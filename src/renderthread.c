#include "renderthread.h"

#include "mimalloc.h"
#include <stdbool.h>

#include "allocator.h"
#include "profiling.h"

#include "config.h"
#include "tbcommon.h"
#include "tbsdl.h"
#include "tbvk.h"
#include "tbvkalloc.h"
#include "vk_mem_alloc.h"
#include "vkdbg.h"

int32_t render_thread(void *data);

// Public API

bool tb_start_render_thread(RenderThreadDescriptor *desc,
                            RenderThread *thread) {
  TB_CHECK_RETURN(desc, "Invalid RenderThreadDescriptor", false);
  thread->window = desc->window;
  thread->initialized = SDL_CreateSemaphore(0);
  thread->thread = SDL_CreateThread(render_thread, "Render Thread", thread);
  TB_CHECK_RETURN(thread->thread, "Failed to create render thread", false);
  return true;
}

void tb_signal_render(RenderThread *thread, uint32_t frame_idx) {
  TB_CHECK(frame_idx < TB_MAX_FRAME_STATES, "Invalid frame index");
  SDL_SemPost(thread->frame_states[frame_idx].wait_sem);
}

void tb_wait_render(RenderThread *thread, uint32_t frame_idx) {
  TB_CHECK(frame_idx < TB_MAX_FRAME_STATES, "Invalid frame index");
  SDL_SemWait(thread->frame_states[frame_idx].signal_sem);
}

void tb_wait_thread_initialized(RenderThread *thread) {
  SDL_SemWait(thread->initialized);
}

void tb_stop_render_thread(RenderThread *thread) {
  uint32_t frame_idx = thread->frame_idx;
  // Wait for render thread
  SDL_SemWait(thread->frame_states[frame_idx].signal_sem);
  // Set the stop signal
  thread->stop_signal = 1;
  // Signal Render thread
  SDL_SemPost(thread->frame_states[frame_idx].wait_sem);

  // Wait for the thread to stop
  int32_t thread_code = 0;
  SDL_WaitThread(thread->thread, &thread_code);

  // Wait for the GPU to be done too
  vkQueueWaitIdle(thread->graphics_queue);
  if (thread->graphics_queue_family_index !=
      thread->present_queue_family_index) {
    vkQueueWaitIdle(thread->present_queue);
  }
}

void destroy_frame_states(VkDevice device, VmaAllocator vma_alloc,
                          const VkAllocationCallbacks *vk_alloc,
                          FrameState *states);

void tb_destroy_render_thread(RenderThread *thread) {
  TB_CHECK(thread, "Invalid thread");

  const VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;
  Allocator std_alloc = thread->std_alloc.alloc;

  vmaDestroyAllocator(thread->vma_alloc);

  vkDestroyDevice(thread->device, vk_alloc);

  tb_free(std_alloc, thread->queue_props);

  // Destroy debug messenger
#ifdef VALIDATION
  vkDestroyDebugUtilsMessengerEXT(thread->instance,
                                  thread->debug_utils_messenger, vk_alloc);
#endif

  SDL_DestroySemaphore(thread->initialized);

  // Clean up the last of the thread
  SDL_DestroyWindow(thread->window);

  vkDestroySurfaceKHR(thread->instance, thread->surface, NULL);

  vkDestroyInstance(thread->instance, &thread->vk_alloc);

  *thread = (RenderThread){0};
}

// Private internals

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifdef VALIDATION
static bool check_layer(const char *check_name, uint32_t layer_count,
                        VkLayerProperties *layers) {
  bool found = false;
  for (uint32_t i = 0; i < layer_count; i++) {
    if (!strcmp(check_name, layers[i].layerName)) {
      found = true;
      break;
    }
  }
  return found;
}

static VkBool32
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                  void *pUserData) {
  (void)messageTypes;
  (void)pUserData;

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity &
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else {
    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  }

  // Helper for breaking when encountering a non-info message
  if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    SDL_TriggerBreakpoint();
  }

  return false;
}
#endif

bool init_instance(SDL_Window *window, Allocator tmp_alloc,
                   const VkAllocationCallbacks *vk_alloc,
                   VkInstance *instance) {
  VkResult err = VK_SUCCESS;

  // Create vulkan instance
  {
    // Gather required layers
    uint32_t layer_count = 0;
    const char *layer_names[MAX_LAYER_COUNT] = {0};

    {
      uint32_t instance_layer_count = 0;
      err = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
      TB_CHECK_RETURN(err == VK_SUCCESS,
                      "Failed to enumerate instance layer property count",
                      false);
      if (instance_layer_count > 0) {
        VkLayerProperties *instance_layers =
            tb_alloc_nm_tp(tmp_alloc, instance_layer_count, VkLayerProperties);
        err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                                                 instance_layers);
        TB_CHECK_RETURN(err == VK_SUCCESS,
                        "Failed to enumerate instance layer properties", false);
#ifdef VALIDATION
        {
          const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

          bool validation_found = check_layer(
              validation_layer_name, instance_layer_count, instance_layers);
          if (validation_found) {
            TB_CHECK_RETURN(layer_count + 1 < MAX_LAYER_COUNT,
                            "Layer count out of range", false);
            layer_names[layer_count++] = validation_layer_name;
          }
        }
#endif
      }
    }

    // Query SDL for required extensions
    uint32_t ext_count = 0;
    const char *ext_names[MAX_EXT_COUNT] = {0};
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, NULL);

    SDL_assert(ext_count < MAX_EXT_COUNT);
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, ext_names);

// Add debug ext
#ifdef VALIDATION
    {
      SDL_assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif

// Add portability for apple devices
#ifdef __APPLE__
    {
      SDL_assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    }
#endif

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Toybox";
    app_info.applicationVersion = VK_MAKE_VERSION(
        TB_GAME_VERSION_MAJOR, TB_GAME_VERSION_MINOR, TB_GAME_VERSION_PATCH);
    app_info.pEngineName = TB_ENGINE_NAME;
    app_info.engineVersion =
        VK_MAKE_VERSION(TB_ENGINE_VERSION_MAJOR, TB_ENGINE_VERSION_MINOR,
                        TB_ENGINE_VERSION_PATCH);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    // Only use this portability bit when necessary. Some older system
    // header versions of vulkan may not support it.
#if defined(VK_USE_PLATFORM_MACOS_MVK) && (VK_HEADER_VERSION >= 216)
    create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;

    err = vkCreateInstance(&create_info, vk_alloc, instance);
    TB_CHECK_RETURN(err == VK_SUCCESS, "Failed to create vulkan instance",
                    false);
  }

  volkLoadInstance(*instance);
  return true;
}

bool init_debug_messenger(VkInstance instance,
                          const VkAllocationCallbacks *vk_alloc,
                          VkDebugUtilsMessengerEXT *debug) {
  // Load debug callback
#ifdef VALIDATION
  VkDebugUtilsMessengerCreateInfoEXT ext_info = {0};
  ext_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  ext_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  ext_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  ext_info.pfnUserCallback = vk_debug_callback;
  VkResult err =
      vkCreateDebugUtilsMessengerEXT(instance, &ext_info, vk_alloc, debug);
  TB_CHECK_RETURN(err == VK_SUCCESS, "Failed to create debug utils messenger",
                  false);
#endif
  return true;
}

bool init_frame_states(VkPhysicalDevice gpu, VkDevice device,
                       const Swapchain *swapchain, VkQueue graphics_queue,
                       uint32_t graphics_queue_family_index,
                       const VkPhysicalDeviceMemoryProperties *gpu_mem_props,
                       VmaAllocator vma_alloc,
                       const VkAllocationCallbacks *vk_alloc,
                       FrameState *states) {
  TB_CHECK_RETURN(states, "Invalid states", false);
  VkResult err = VK_SUCCESS;

  uint32_t swap_img_count = 0;
  vkGetSwapchainImagesKHR(device, swapchain->swapchain, &swap_img_count, NULL);
  TB_VK_CHECK_RET(err, "Failed to get swapchain image count", false);
  TB_CHECK_RETURN(swap_img_count >= TB_MAX_FRAME_STATES,
                  "Fewer than required swapchain images", false);
  if (swap_img_count > TB_MAX_FRAME_STATES) {
    swap_img_count = TB_MAX_FRAME_STATES;
  }

  VkImage swapchain_images[TB_MAX_FRAME_STATES] = {0};
  vkGetSwapchainImagesKHR(device, swapchain->swapchain, &swap_img_count,
                          swapchain_images);
  TB_VK_CHECK_RET(err, "Failed to get swapchain images", false);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    FrameState *state = &states[i];

    state->wait_sem = SDL_CreateSemaphore(1);
    TB_CHECK_RETURN(state->wait_sem,
                    "Failed to create frame state wait semaphore", false);
    state->signal_sem = SDL_CreateSemaphore(0);
    TB_CHECK_RETURN(state->signal_sem,
                    "Failed to create frame state signal semaphore", false);

    {
      VkCommandPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .queueFamilyIndex = graphics_queue_family_index,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      };
      err = vkCreateCommandPool(device, &create_info, vk_alloc,
                                &state->command_pool);
      TB_VK_CHECK_RET(err, "Failed to create frame state command pool", false);
      SET_VK_NAME(device, state->command_pool, VK_OBJECT_TYPE_COMMAND_POOL,
                  "Frame State Command Pool");
    }

    {
      VkCommandBufferAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1,
          .commandPool = state->command_pool,
      };
      err =
          vkAllocateCommandBuffers(device, &alloc_info, &state->command_buffer);
      TB_VK_CHECK_RET(err, "Failed to create frame state command buffer",
                      false);
      SET_VK_NAME(device, state->command_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER,
                  "Frame State Command Buffer");
    }

    {
      state->swapchain_image = swapchain_images[i];
      SET_VK_NAME(device, state->swapchain_image, VK_OBJECT_TYPE_IMAGE,
                  "Frame State Swapchain Image");

      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = swapchain->format,
          .image = state->swapchain_image,
          .components =
              (VkComponentMapping){
                  VK_COMPONENT_SWIZZLE_R,
                  VK_COMPONENT_SWIZZLE_G,
                  VK_COMPONENT_SWIZZLE_B,
                  VK_COMPONENT_SWIZZLE_A,
              },
          .subresourceRange =
              (VkImageSubresourceRange){
                  VK_IMAGE_ASPECT_COLOR_BIT,
                  0,
                  1,
                  0,
                  1,
              },
      };
      err = vkCreateImageView(device, &create_info, vk_alloc,
                              &state->swapchain_image_view);
      TB_VK_CHECK_RET(err, "Failed to create frame state swapchain image view",
                      false);
      SET_VK_NAME(device, state->swapchain_image_view,
                  VK_OBJECT_TYPE_IMAGE_VIEW,
                  "Frame State Swapchain Image View");
    }

    {
      VkSemaphoreCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      };

      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &state->img_acquired_sem);
      TB_VK_CHECK_RET(
          err, "Failed to create swapchain image acquired semaphore", false);
      SET_VK_NAME(device, state->img_acquired_sem, VK_OBJECT_TYPE_SEMAPHORE,
                  "Frame State Swapchain Image Acquired Sem");

      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &state->swapchain_image_sem);
      TB_VK_CHECK_RET(err, "Failed to create swapchain image semaphore", false);
      SET_VK_NAME(device, state->swapchain_image_sem, VK_OBJECT_TYPE_SEMAPHORE,
                  "Frame State Swapchain Image Sem");

      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &state->render_complete_sem);
      TB_VK_CHECK_RET(err, "Failed to create render complete semaphore", false);
      SET_VK_NAME(device, state->render_complete_sem, VK_OBJECT_TYPE_SEMAPHORE,
                  "Frame State Render Complete Sem");
    }

    {
      VkFenceCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
          .flags = VK_FENCE_CREATE_SIGNALED_BIT,
      };
      err = vkCreateFence(device, &create_info, vk_alloc, &state->fence);
      TB_VK_CHECK_RET(err, "Failed to create fence", false);
      SET_VK_NAME(device, state->fence, VK_OBJECT_TYPE_FENCE,
                  "Frame State Fence");
    }

    {
      TracyCGPUContext *gpu_ctx =
          TracyCVkContextExt(gpu, device, graphics_queue, state->command_buffer,
                             vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
                             vkGetCalibratedTimestampsEXT);
      const char *name = "Frame State GPU Context";
      TracyCVkContextName(gpu_ctx, name, SDL_strlen(name));
      state->tracy_gpu_context = gpu_ctx;
    }

    const uint64_t size_bytes = TB_VMA_TMP_GPU_MB * 1024 * 1024;

    {
      uint32_t gpu_mem_type_idx = 0xFFFFFFFF;
      // Find the desired memory type index
      for (uint32_t i = 0; i < gpu_mem_props->memoryTypeCount; ++i) {
        VkMemoryType type = gpu_mem_props->memoryTypes[i];
        if (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
          gpu_mem_type_idx = i;
          break;
        }
      }
      TB_CHECK_RETURN(gpu_mem_type_idx != 0xFFFFFFFF,
                      "Failed to find gpu visible memory", false);

      VmaPoolCreateInfo create_info = {
          .memoryTypeIndex = gpu_mem_type_idx,
          .flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
          .maxBlockCount = 1,
          .blockSize = size_bytes,
      };
      err = vmaCreatePool(vma_alloc, &create_info, &state->tmp_gpu_pool);
      TB_VK_CHECK_RET(err, "Failed to create vma temp host pool", false);
    }

    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = size_bytes,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      };
      VmaAllocationCreateInfo alloc_create_info = {
          .pool = state->tmp_gpu_pool,
          .usage = VMA_MEMORY_USAGE_GPU_ONLY,
      };
      VmaAllocationInfo alloc_info = {0};
      err = vmaCreateBuffer(vma_alloc, &create_info, &alloc_create_info,
                            &state->tmp_gpu_buffer, &state->tmp_gpu_alloc,
                            &alloc_info);
      TB_VK_CHECK_RET(err, "Failed to create vma temp gpu buffer", false);
      SET_VK_NAME(device, state->tmp_gpu_buffer, VK_OBJECT_TYPE_BUFFER,
                  "Vulkan Tmp GPU Buffer");
    }
  }

  return true;
}

void destroy_frame_states(VkDevice device, VmaAllocator vma_alloc,
                          const VkAllocationCallbacks *vk_alloc,
                          FrameState *states) {
  TB_CHECK(states, "Invalid states");

  vkDeviceWaitIdle(device);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    FrameState *state = &states[i];

    SDL_DestroySemaphore(state->wait_sem);
    SDL_DestroySemaphore(state->signal_sem);

    vkFreeCommandBuffers(device, state->command_pool, 1,
                         &state->command_buffer);
    vkDestroyCommandPool(device, state->command_pool, vk_alloc);

    TracyCVkContextDestroy(state->tracy_gpu_context);

    // swapchain_image is owned by the KHR swapchain and doesn't need to be
    // freed
    vkDestroyImageView(device, state->swapchain_image_view, vk_alloc);

    vkDestroySemaphore(device, state->img_acquired_sem, vk_alloc);
    vkDestroySemaphore(device, state->swapchain_image_sem, vk_alloc);
    vkDestroySemaphore(device, state->render_complete_sem, vk_alloc);

    vkDestroyFence(device, state->fence, vk_alloc);

    vmaDestroyBuffer(vma_alloc, state->tmp_gpu_buffer, state->tmp_gpu_alloc);
    vmaDestroyPool(vma_alloc, state->tmp_gpu_pool);

    *state = (FrameState){0};
  }
}

bool init_gpu(VkInstance instance, Allocator std_alloc, Allocator tmp_alloc,
              VkPhysicalDevice *gpu, VkPhysicalDeviceProperties2 *gpu_props,
              VkPhysicalDeviceDriverProperties *driver_props,
              uint32_t *queue_family_count,
              VkQueueFamilyProperties **queue_props,
              VkPhysicalDeviceFeatures *gpu_features,
              VkPhysicalDeviceMemoryProperties *gpu_mem_props) {
  uint32_t gpu_count = 0;
  VkResult err = vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
  TB_CHECK_RETURN(err == VK_SUCCESS, "Failed to enumerate gpu count", false);

  VkPhysicalDevice *gpus =
      tb_alloc_nm_tp(tmp_alloc, gpu_count, VkPhysicalDevice);
  err = vkEnumeratePhysicalDevices(instance, &gpu_count, gpus);
  TB_CHECK_RETURN(err == VK_SUCCESS, "Failed to enumerate gpus", false);

  /* Try to auto select most suitable device */
  int32_t gpu_idx = -1;
  {
    uint32_t count_device_type[VK_PHYSICAL_DEVICE_TYPE_CPU + 1];
    SDL_memset(count_device_type, 0, sizeof(count_device_type));

    VkPhysicalDeviceProperties gpu_props = {0};
    for (uint32_t i = 0; i < gpu_count; i++) {
      vkGetPhysicalDeviceProperties(gpus[i], &gpu_props);
      TB_CHECK_RETURN(gpu_props.deviceType <= VK_PHYSICAL_DEVICE_TYPE_CPU,
                      "Unexpected gpu type", false);

      count_device_type[gpu_props.deviceType]++;
    }

    VkPhysicalDeviceType search_for_device_type =
        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_CPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_CPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_OTHER]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    }

    for (uint32_t i = 0; i < gpu_count; i++) {
      vkGetPhysicalDeviceProperties(gpus[i], &gpu_props);
      if (gpu_props.deviceType == search_for_device_type) {
        gpu_idx = (int32_t)i;
        break;
      }
    }
  }

  TB_CHECK_RETURN(gpu_idx >= 0, "Failed to find suitable gpu", false);
  *gpu = gpus[gpu_idx];

  // Check physical device properties
  *driver_props = (VkPhysicalDeviceDriverProperties){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  *gpu_props = (VkPhysicalDeviceProperties2){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = driver_props};
  vkGetPhysicalDeviceProperties2(*gpu, gpu_props);

  vkGetPhysicalDeviceQueueFamilyProperties(*gpu, queue_family_count, NULL);

  (*queue_props) =
      tb_alloc_nm_tp(std_alloc, *queue_family_count, VkQueueFamilyProperties);
  TB_CHECK_RETURN(queue_props, "Failed to allocate queue props", false);
  vkGetPhysicalDeviceQueueFamilyProperties(*gpu, queue_family_count,
                                           (*queue_props));

  vkGetPhysicalDeviceFeatures(*gpu, gpu_features);

  vkGetPhysicalDeviceMemoryProperties(*gpu, gpu_mem_props);

  return true;
}

bool init_surface(VkInstance instance, SDL_Window *window,
                  VkSurfaceKHR *surface) {
  // SDL subsystems will clean up this surface on their own
  TB_CHECK_RETURN(SDL_Vulkan_CreateSurface(window, instance, surface),
                  "Failed to create surface", false);
  return true;
}

bool find_queue_families(Allocator tmp_alloc, VkPhysicalDevice gpu,
                         VkSurfaceKHR surface, uint32_t queue_family_count,
                         const VkQueueFamilyProperties *queue_props,
                         uint32_t *present_queue_family_index,
                         uint32_t *graphics_queue_family_index) {
  uint32_t graphics_idx = 0xFFFFFFFF;
  uint32_t present_idx = 0xFFFFFFFF;
  {
    // Iterate over each queue to learn whether it supports presenting:
    VkBool32 *supports_present =
        tb_alloc_nm_tp(tmp_alloc, queue_family_count, VkBool32);
    for (uint32_t i = 0; i < queue_family_count; i++) {
      vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface,
                                           &supports_present[i]);
    }

    // Search for a graphics and a present queue in the array of queue
    // families, try to find one that supports both
    for (uint32_t i = 0; i < queue_family_count; i++) {
      if ((queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        if (graphics_idx == 0xFFFFFFFF) {
          graphics_idx = i;
        }

        if (supports_present[i] == VK_TRUE) {
          graphics_idx = i;
          present_idx = i;
          break;
        }
      }
    }

    if (present_idx == 0xFFFFFFFF) {
      // If didn't find a queue that supports both graphics and present, then
      // find a separate present queue.
      for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (supports_present[i] == VK_TRUE) {
          present_idx = i;
          break;
        }
      }
    }

    // Generate error if could not find both a graphics and a present queue
    TB_CHECK_RETURN(graphics_idx != 0xFFFFFFFF && present_idx != 0xFFFFFFFF,
                    "Invalid queue family indices", false);
  }
  *present_queue_family_index = present_idx;
  *graphics_queue_family_index = graphics_idx;
  return true;
}

bool device_supports_ext(const VkExtensionProperties *props,
                         uint32_t prop_count, const char *ext_name) {
  for (uint32_t i = 0; i < prop_count; ++i) {
    const VkExtensionProperties *prop = &props[i];
    if (SDL_strcmp(prop->extensionName, ext_name) == 0) {
      return true;
    }
  }

  return false;
}

void required_device_ext(const char **out_ext_names, uint32_t *out_ext_count,
                         const VkExtensionProperties *props,
                         uint32_t prop_count, const char *ext_name) {
  if (device_supports_ext(props, prop_count, ext_name)) {
    SDL_assert((*out_ext_count + 1) < MAX_EXT_COUNT);
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Loading required extension: %s",
                ext_name);
    out_ext_names[(*out_ext_count)++] = ext_name;
    return;
  }

  SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Missing required extension: %s",
               ext_name);
  SDL_TriggerBreakpoint();
}

bool optional_device_ext(const char **out_ext_names, uint32_t *out_ext_count,
                         const VkExtensionProperties *props,
                         uint32_t prop_count, const char *ext_name) {
  if (device_supports_ext(props, prop_count, ext_name)) {
    SDL_assert((*out_ext_count + 1) < MAX_EXT_COUNT);
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Loading optional extension: %s",
                ext_name);
    out_ext_names[(*out_ext_count)++] = ext_name;
    return true;
  }

  SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Optional extension not supported: %s",
              ext_name);
  return false;
}

bool init_device(VkPhysicalDevice gpu, uint32_t graphics_queue_family_index,
                 uint32_t present_queue_family_index, Allocator tmp_alloc,
                 const VkAllocationCallbacks *vk_alloc,
                 RenderExtensionSupport *ext_support, VkDevice *device) {
  VkResult err = VK_SUCCESS;

  uint32_t device_ext_count = 0;
  const char *device_ext_names[MAX_EXT_COUNT] = {0};

  RenderExtensionSupport ext = {0};
  {
    const uint32_t max_props = 256;
    VkExtensionProperties *props =
        tb_alloc_nm_tp(tmp_alloc, max_props, VkExtensionProperties);

    uint32_t prop_count = 0;
    err = vkEnumerateDeviceExtensionProperties(gpu, "", &prop_count, NULL);
    TB_CHECK_RETURN(err == VK_SUCCESS,
                    "Failed to enumerate device extension property count",
                    false);

    TB_CHECK_RETURN(prop_count < max_props,
                    "Device extension property count out of range", false);

    err = vkEnumerateDeviceExtensionProperties(gpu, "", &prop_count, props);
    TB_CHECK_RETURN(err == VK_SUCCESS,
                    "Failed to enumerate device extension properties", false);

    // Only need portability on macos / ios
#if (defined(VK_USE_PLATFORM_MACOS_MVK) ||                                     \
     defined(VK_USE_PLATFORM_IOS_MVK)) &&                                      \
    (VK_HEADER_VERSION >= 216)
    ext.portability = optional_device_ext(
        &device_ext_names, &device_ext_count, props, prop_count,
        VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    // Need a swapchain
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Raytracing is optional
#if defined(VK_KHR_ray_tracing_pipeline)
    if (optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                            props, prop_count,
                            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
      ext.raytracing = true;

      // Required for Spirv 1.4
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count,
                          VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

      // Required for VK_KHR_ray_tracing_pipeline
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count, VK_KHR_SPIRV_1_4_EXTENSION_NAME);

      // Required for VK_KHR_acceleration_structure
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count,
                          VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count,
                          VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count,
                          VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

      // Required for raytracing
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count,
                          VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count, VK_KHR_RAY_QUERY_EXTENSION_NAME);
      optional_device_ext((const char **)&device_ext_names, &device_ext_count,
                          props, prop_count,
                          VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
    }
#endif

#ifdef TRACY_ENABLE
    // Enable calibrated timestamps if we can when profiling with tracy
    {
      ext.calibrated_timestamps = optional_device_ext(
          (const char **)&device_ext_names, &device_ext_count, props,
          prop_count, VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    }
#endif
  }

  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queues[2];
  queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queues[0].pNext = NULL;
  queues[0].queueFamilyIndex = graphics_queue_family_index;
  queues[0].queueCount = 1;
  queues[0].pQueuePriorities = queue_priorities;
  queues[0].flags = 0;

#if defined(VK_KHR_ray_tracing_pipeline)
  VkPhysicalDeviceRayQueryFeaturesKHR rt_query_feature = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
      .rayQuery = ext_support->raytracing,
  };

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipe_feature = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
      .rayTracingPipeline = ext_support->raytracing,
      .pNext = &rt_query_feature,
  };
#endif

  VkPhysicalDeviceVulkan11Features vk_11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .multiview = VK_TRUE,
      .pNext = &rt_pipe_feature,
  };

  VkPhysicalDeviceVulkan12Features vk_12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .descriptorIndexing = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .pNext = &vk_11_features,
  };

  VkDeviceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pNext = (const void *)&vk_12_features;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = queues;
  create_info.enabledExtensionCount = device_ext_count;
  create_info.ppEnabledExtensionNames = device_ext_names;

  if (present_queue_family_index != graphics_queue_family_index) {
    queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[1].pNext = NULL;
    queues[1].queueFamilyIndex = present_queue_family_index;
    queues[1].queueCount = 1;
    queues[1].pQueuePriorities = queue_priorities;
    queues[1].flags = 0;
    create_info.queueCreateInfoCount = 2;
  }

  err = vkCreateDevice(gpu, &create_info, vk_alloc, device);
  TB_CHECK_RETURN(err == VK_SUCCESS, "Failed to create device", false);

  SET_VK_NAME(*device, *device, VK_OBJECT_TYPE_DEVICE, "Toybox Vulkan Device");
  SET_VK_NAME(*device, gpu, VK_OBJECT_TYPE_PHYSICAL_DEVICE, "Toybox GPU");

  *ext_support = ext;

  return true;
}

bool init_queues(VkDevice device, uint32_t graphics_queue_family_index,
                 uint32_t present_queue_family_index, VkQueue *graphics_queue,
                 VkQueue *present_queue) {
  vkGetDeviceQueue(device, graphics_queue_family_index, 0, graphics_queue);

  if (graphics_queue_family_index == present_queue_family_index) {
    *present_queue = *graphics_queue;
    SET_VK_NAME(device, *graphics_queue, VK_OBJECT_TYPE_QUEUE,
                "Graphics & Present Queue");
  } else {
    vkGetDeviceQueue(device, present_queue_family_index, 0, present_queue);
    SET_VK_NAME(device, *graphics_queue, VK_OBJECT_TYPE_QUEUE,
                "Graphics Queue");
    SET_VK_NAME(device, *present_queue, VK_OBJECT_TYPE_QUEUE, "Present Queue");
  }

  return true;
}

bool init_vma(VkInstance instance, VkPhysicalDevice gpu, VkDevice device,
              const VkAllocationCallbacks *vk_alloc, VmaAllocator *vma_alloc) {
  VmaVulkanFunctions volk_functions = {
      .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
      .vkGetPhysicalDeviceMemoryProperties =
          vkGetPhysicalDeviceMemoryProperties,
      .vkAllocateMemory = vkAllocateMemory,
      .vkFreeMemory = vkFreeMemory,
      .vkMapMemory = vkMapMemory,
      .vkUnmapMemory = vkUnmapMemory,
      .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
      .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
      .vkBindBufferMemory = vkBindBufferMemory,
      .vkBindImageMemory = vkBindImageMemory,
      .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
      .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
      .vkCreateBuffer = vkCreateBuffer,
      .vkDestroyBuffer = vkDestroyBuffer,
      .vkCreateImage = vkCreateImage,
      .vkDestroyImage = vkDestroyImage,
      .vkCmdCopyBuffer = vkCmdCopyBuffer,
  };
  VmaDeviceMemoryCallbacks vma_callbacks = {
      tb_vma_alloc_fn,
      tb_vma_free_fn,
      NULL,
  };
  VmaAllocatorCreateInfo create_info = {
      .physicalDevice = gpu,
      .device = device,
      .pVulkanFunctions = &volk_functions,
      .instance = instance,
      .vulkanApiVersion = VK_API_VERSION_1_0,
      .pAllocationCallbacks = vk_alloc,
      .pDeviceMemoryCallbacks = &vma_callbacks,
  };

  VkResult err = vmaCreateAllocator(&create_info, vma_alloc);
  TB_VK_CHECK_RET(err, "Failed to create vma allocator", false);
  return true;
}

bool init_swapchain(SDL_Window *window, VkDevice device, VkPhysicalDevice gpu,
                    VkSurfaceKHR surface, Allocator tmp_alloc,
                    const VkAllocationCallbacks *vk_alloc,
                    Swapchain *swapchain) {
  int32_t width = 0;
  int32_t height = 0;
  SDL_Vulkan_GetDrawableSize(window, &width, &height);

  uint32_t format_count = 0;
  VkResult err =
      vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
  TB_VK_CHECK_RET(err, "Failed to get physical device surface format count",
                  false);
  VkSurfaceFormatKHR *surface_formats =
      tb_alloc_nm_tp(tmp_alloc, format_count, VkSurfaceFormatKHR);
  err = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count,
                                             surface_formats);
  TB_VK_CHECK_RET(err, "Failed to get physical device surface formats", false);

  VkSurfaceFormatKHR surface_format = surface_formats[0];
  // See if we can find a better surface format
  {
    for (uint32_t i = 0; i < format_count; i++) {
      const VkFormat format = surface_formats[i].format;

      if (format == VK_FORMAT_R8G8B8A8_UNORM ||
          format == VK_FORMAT_B8G8R8A8_UNORM ||
          format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
          format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 ||
          format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        surface_format = surface_formats[i];
        break;
      }
    }
  }

  VkSurfaceCapabilitiesKHR surf_caps;
  err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surf_caps);
  TB_VK_CHECK_RET(err, "Failed to get physical device surface caps", false);

  uint32_t present_mode_count = 0;
  err = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface,
                                                  &present_mode_count, NULL);
  TB_VK_CHECK_RET(
      err, "Failed to get physical device surface present mode count", false);
  VkPresentModeKHR *present_modes =
      tb_alloc_nm_tp(tmp_alloc, present_mode_count, VkPresentModeKHR);
  TB_CHECK_RETURN(present_modes, "Failed to allocate tmp present modes", false);
  err = vkGetPhysicalDeviceSurfacePresentModesKHR(
      gpu, surface, &present_mode_count, present_modes);
  TB_VK_CHECK_RET(err, "Failed to get physical device surface present modes",
                  false);

  VkExtent2D swapchain_extent = {
      .width = (uint32_t)width,
      .height = (uint32_t)height,
  };

  // The FIFO present mode is guaranteed by the spec to be supported
  // and to have no tearing.  It's a great default present mode to use.
  VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  if (present_mode != swapchain_present_mode) {
    for (uint32_t i = 0; i < present_mode_count; ++i) {
      if (present_modes[i] == present_mode) {
        swapchain_present_mode = present_mode;
        break;
      }
    }
  }
  if (swapchain_present_mode != present_mode) {
    // The desired present mode was not found, just use the first one
    present_mode = present_modes[0];
  }
  present_modes = NULL;

  // Determine the number of VkImages to use in the swap chain.
  // Application desires to acquire 3 images at a time for triple
  // buffering
  uint32_t image_count = TB_MAX_FRAME_STATES;
  if (image_count < surf_caps.minImageCount) {
    image_count = surf_caps.minImageCount;
  }
  // If maxImageCount is 0, we can ask for as many images as we want;
  // otherwise we're limited to maxImageCount
  if ((surf_caps.maxImageCount > 0) &&
      (image_count > surf_caps.maxImageCount)) {
    // Application must settle for fewer images than desired:
    image_count = surf_caps.maxImageCount;
  }

  VkSurfaceTransformFlagBitsKHR pre_transform;
  if (surf_caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    pre_transform = surf_caps.currentTransform;
  }

  // Find a supported composite alpha mode - one of these is guaranteed to
  // be set
  VkCompositeAlphaFlagBitsKHR composite_alpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (uint32_t i = 0; i < 4; i++) {
    if (surf_caps.supportedCompositeAlpha &
        (VkCompositeAlphaFlagsKHR)composite_alpha_flags[i]) {
      composite_alpha = composite_alpha_flags[i];
      break;
    }
  }

  VkSwapchainCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
  // On Android, vkGetSwapchainImagesKHR is always returning 1 more image than
  // our min image count
#ifdef __ANDROID__
      .minImageCount = image_count - 1,
#else
      .minImageCount = image_count,
#endif
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = swapchain_extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .compositeAlpha = composite_alpha,
      .preTransform = pre_transform,
      .presentMode = present_mode,
      .oldSwapchain = swapchain->swapchain,
  };

  VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
  err = vkCreateSwapchainKHR(device, &create_info, vk_alloc, &vk_swapchain);
  TB_VK_CHECK_RET(err, "Failed to create swapchain", false);
  SET_VK_NAME(device, vk_swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR, "Swapchain");

  *swapchain = (Swapchain){
      .valid = true,
      .format = surface_format.format,
      .color_space = surface_format.colorSpace,
      .present_mode = present_mode,
      .image_count = image_count,
      .width = swapchain_extent.width,
      .height = swapchain_extent.height,
      .swapchain = vk_swapchain,
  };
  return true;
}

bool init_render_thread(RenderThread *thread) {
  TB_CHECK_RETURN(thread, "Invalid render thread", false);
  TB_CHECK_RETURN(thread->window, "Render thread given no window", false);

  VkResult err = VK_SUCCESS;

  // Create renderer allocators
  {
    const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
    create_arena_allocator("Render Arena", &thread->render_arena,
                           arena_alloc_size);

    create_standard_allocator(&thread->std_alloc, "Render Std Alloc");
  }

  err = volkInitialize();
  TB_CHECK_RETURN(err == VK_SUCCESS, "Failed to initialize volk", false);

  // Create vulkan allocator
  thread->vk_alloc = (VkAllocationCallbacks){
      .pfnAllocation = tb_vk_alloc_fn,
      .pfnReallocation = tb_vk_realloc_fn,
      .pfnFree = tb_vk_free_fn,
  };

  Allocator std_alloc = thread->std_alloc.alloc;
  Allocator tmp_alloc = thread->render_arena.alloc;
  const VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;

  TB_CHECK_RETURN(
      init_instance(thread->window, tmp_alloc, vk_alloc, &thread->instance),
      "Failed to create instance", false);

  TB_CHECK_RETURN(init_debug_messenger(thread->instance, vk_alloc,
                                       &thread->debug_utils_messenger),
                  "Failed to create debug messenger", false);

  TB_CHECK_RETURN(init_gpu(thread->instance, std_alloc, tmp_alloc, &thread->gpu,
                           &thread->gpu_props, &thread->driver_props,
                           &thread->queue_family_count, &thread->queue_props,
                           &thread->gpu_features, &thread->gpu_mem_props),
                  "Failed to select gpu", false)

  TB_CHECK_RETURN(
      init_surface(thread->instance, thread->window, &thread->surface),
      "Failed to create surface", false);

  TB_CHECK_RETURN(find_queue_families(tmp_alloc, thread->gpu, thread->surface,
                                      thread->queue_family_count,
                                      thread->queue_props,
                                      &thread->present_queue_family_index,
                                      &thread->graphics_queue_family_index),
                  "Failed to get find queue families", false);

  TB_CHECK_RETURN(init_device(thread->gpu, thread->graphics_queue_family_index,
                              thread->present_queue_family_index, tmp_alloc,
                              vk_alloc, &thread->ext_support, &thread->device),
                  "Failed to init device", false);

  TB_CHECK_RETURN(init_queues(thread->device,
                              thread->graphics_queue_family_index,
                              thread->present_queue_family_index,
                              &thread->graphics_queue, &thread->present_queue),
                  "Failed to init queues", false);

  TB_CHECK_RETURN(init_vma(thread->instance, thread->gpu, thread->device,
                           vk_alloc, &thread->vma_alloc),
                  "Failed to init the Vulkan Memory Allocator", false);

  TB_CHECK_RETURN(init_swapchain(thread->window, thread->device, thread->gpu,
                                 thread->surface, tmp_alloc, vk_alloc,
                                 &thread->swapchain),
                  "Failed to init swapchain", false);

  TB_CHECK_RETURN(init_frame_states(thread->gpu, thread->device,
                                    &thread->swapchain, thread->graphics_queue,
                                    thread->graphics_queue_family_index,
                                    &thread->gpu_mem_props, thread->vma_alloc,
                                    vk_alloc, thread->frame_states),
                  "Failed to init frame states", false);

  return true;
}

void resize_swapchain(void) {
  // TODO
}

void tick_render_thread(RenderThread *thread, FrameState *state) {
  VkResult err = VK_SUCCESS;

  VkDevice device = thread->device;

  VkQueue graphics_queue = thread->graphics_queue;
  VkQueue present_queue = thread->present_queue;

  VkSemaphore img_acquired_sem = state->img_acquired_sem;
  VkSemaphore render_complete_sem = state->render_complete_sem;
  VkSemaphore swapchain_image_sem = state->swapchain_image_sem;
  VkFence fence = state->fence;

  VkCommandBuffer command_buffer = state->command_buffer;

  // Ensure the frame state we're about to use isn't being handled by the GPU
  {
    TracyCZoneN(fence_ctx, "Wait for GPU", true);
    TracyCZoneColor(fence_ctx, TracyCategoryColorWait);
    vkWaitForFences(thread->device, 1, &fence, VK_TRUE, SDL_MAX_UINT64);
    TracyCZoneEnd(fence_ctx);

    vkResetFences(device, 1, &fence);
  }

  // Acquire Image
  {
    TracyCZoneN(acquire_ctx, "Acquired Next Swapchain Image", true);
    do {
      err = vkAcquireNextImageKHR(device, thread->swapchain.swapchain,
                                  SDL_MIN_UINT64, img_acquired_sem,
                                  VK_NULL_HANDLE, &thread->frame_idx);
      if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        // swapchain is out of date (e.g. the window was resized) and
        // must be recreated:
        resize_swapchain();
      } else if (err == VK_SUBOPTIMAL_KHR) {
        // swapchain is not as optimal as it could be, but the
        // platform's presentation engine will still present the image
        // correctly.
        break;
      } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
        // If the surface was lost we could re-create it.
        // But the surface is owned by SDL2
        SDL_assert(err == VK_SUCCESS);
      } else {
        SDL_assert(err == VK_SUCCESS);
      }
    } while (err != VK_SUCCESS);
    TracyCZoneEnd(acquire_ctx);
  }

  // Reset Pool
  {
    TracyCZoneN(pool_ctx, "Reset Pool", true);
    TracyCZoneColor(pool_ctx, TracyCategoryColorRendering);

    vkResetCommandPool(device, state->command_pool, 0);

    TracyCZoneEnd(pool_ctx);
  }

  // Draw
  {
    TracyCZoneN(draw_ctx, "Draw", true);
    TracyCZoneColor(draw_ctx, TracyCategoryColorRendering);

    {
      VkCommandBufferBeginInfo begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      err = vkBeginCommandBuffer(command_buffer, &begin_info);
      TB_VK_CHECK(err, "Failed to begin command buffer");
    }

    TracyCGPUContext *gpu_ctx = (TracyCGPUContext *)state->tracy_gpu_context;

    // Upload
    {
      TracyCZoneN(up_ctx, "Record Upload", true);
      TracyCVkNamedZone(gpu_ctx, frame_scope, command_buffer, "Upload", 1,
                        true);
      // Upload all buffer requests
      {
        for (uint32_t i = 0; i < state->buf_copy_queue.req_count; ++i) {
          const BufferCopy *up = &state->buf_copy_queue.reqs[i];
          vkCmdCopyBuffer(command_buffer, up->src, up->dst, 1, &up->region);
        }

        state->buf_copy_queue.req_count = 0;
      }

      // Upload all buffer to image requests
      {
        for (uint32_t i = 0; i < state->buf_img_copy_queue.req_count; ++i) {
          const BufferImageCopy *up = &state->buf_img_copy_queue.reqs[i];

          // Transition target image to copy dst
          {
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .subresourceRange =
                    {
                        .baseArrayLayer =
                            up->region.imageSubresource.baseArrayLayer,
                        .baseMipLevel = up->region.imageSubresource.mipLevel,
                        .levelCount = 1,
                        .layerCount = up->region.imageSubresource.layerCount,
                        .aspectMask = up->region.imageSubresource.aspectMask,
                    },
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image = up->dst,
            };
            vkCmdPipelineBarrier(command_buffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                                 NULL, 1, &barrier);
          }

          // Perform the copy
          vkCmdCopyBufferToImage(command_buffer, up->src, up->dst,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                 &up->region);

          // Transition to readable layout
          {
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .subresourceRange =
                    {
                        .baseArrayLayer =
                            up->region.imageSubresource.baseArrayLayer,
                        .baseMipLevel = up->region.imageSubresource.mipLevel,
                        .levelCount = 1,
                        .layerCount = up->region.imageSubresource.layerCount,
                        .aspectMask = up->region.imageSubresource.aspectMask,
                    },
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image = up->dst,
            };
            vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 NULL, 0, NULL, 1, &barrier);
          }
        }

        state->buf_img_copy_queue.req_count = 0;
      }

      TracyCVkZoneEnd(frame_scope);
      TracyCZoneEnd(up_ctx);
    }

    {
      TracyCVkNamedZone(gpu_ctx, frame_scope, command_buffer, "Render", 1,
                        true);

      // Transition swapchain image to color attachment output
      {
        TracyCZoneN(swap_trans_e, "Transition swapchain to color output", true);
        VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (thread->frame_count >= TB_MAX_FRAME_STATES) {
          old_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = old_layout,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = state->swapchain_image,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1,
        };

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1, &barrier);
        TracyCZoneEnd(swap_trans_e);
      }

      // Draw user registered passes
      for (uint32_t pass_idx = 0; pass_idx < state->pass_count; ++pass_idx) {
        PassDrawCtx *ctx = &state->pass_draw_contexts[pass_idx];

        // TODO: Fix assumption that we want the pass to target the swapchain
        VkRenderPassBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx->pass,
            .framebuffer = ctx->framebuffer,
            .renderArea =
                {
                    .extent =
                        {
                            .width = ctx->width,
                            .height = ctx->height,
                        },
                },
            .clearValueCount = 1,
            .pClearValues =
                &(VkClearValue){
                    .color.float32 = {0},
                },
        };

        vkCmdBeginRenderPass(command_buffer, &begin_info,
                             VK_SUBPASS_CONTENTS_INLINE);

        ctx->record_cb(command_buffer, ctx->batch_count, ctx->batches);

        vkCmdEndRenderPass(command_buffer);
      }

      // Transition swapchain image back to presentable
      {
        TracyCZoneN(swap_trans_e, "Transition swapchain to presentable", true);
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = state->swapchain_image,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1,
        };
        vkCmdPipelineBarrier(
            command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        TracyCZoneEnd(swap_trans_e);
      }

      TracyCVkZoneEnd(frame_scope);

      TracyCVkCollect(gpu_ctx, command_buffer);

      err = vkEndCommandBuffer(command_buffer);
      TB_VK_CHECK(err, "Failed to end command buffer");
    }

    TracyCZoneEnd(draw_ctx);
  }

  // Submit
  {
    TracyCZoneN(submit_ctx, "Submit", true);
    TracyCZoneColor(submit_ctx, TracyCategoryColorRendering);

    uint32_t wait_sem_count = 0;
    VkSemaphore wait_sems[16] = {0};
    VkPipelineStageFlags wait_stage_flags[16] = {0};

    wait_sems[wait_sem_count] = img_acquired_sem;
    wait_stage_flags[wait_sem_count++] =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    {
      VkSubmitInfo submit_info = {
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .waitSemaphoreCount = wait_sem_count,
          .pWaitSemaphores = wait_sems,
          .pWaitDstStageMask = wait_stage_flags,
          .commandBufferCount = 1,
          .pCommandBuffers = &command_buffer,
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = &render_complete_sem,
      };

      queue_begin_label(graphics_queue, "raster",
                        (float4){1.0f, 0.1f, 0.1f, 1.0f});
      err = vkQueueSubmit(graphics_queue, 1, &submit_info, state->fence);
      queue_end_label(graphics_queue);
      TB_VK_CHECK(err, "Failed to submit raster work");
    }

    TracyCZoneEnd(submit_ctx);
  }

  // Present
  {
    TracyCZoneN(present_ctx, "Present", true);
    TracyCZoneColor(present_ctx, TracyCategoryColorRendering);

    VkSemaphore wait_sem = render_complete_sem;
    if (thread->present_queue_family_index !=
        thread->graphics_queue_family_index) {
      // If we are using separate queues, change image ownership to the
      // present queue before presenting, waiting for the draw complete
      // semaphore and signalling the ownership released semaphore when
      // finished
      VkSubmitInfo submit_info = {
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &render_complete_sem,
          .signalSemaphoreCount = 1,
          .pSignalSemaphores = &swapchain_image_sem,
      };

      err = vkQueueSubmit(present_queue, 1, &submit_info, VK_NULL_HANDLE);
      TB_VK_CHECK(err, "Failed to submit to queue");

      wait_sem = swapchain_image_sem;
    }

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait_sem,
        .swapchainCount = 1,
        .pSwapchains = &thread->swapchain.swapchain,
        .pImageIndices = &thread->frame_idx,
    };
    err = vkQueuePresentKHR(present_queue, &present_info);

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      // swapchain is out of date (e.g. the window was resized) and
      // must be recreated:
      resize_swapchain();
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // Swapchain is not as optimal as it could be, but the
      // platform's presentation engine will still present the image
      // correctly.
    } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
      // If the surface was lost we could re-create it.
      // But the surface is owned by SDL2
      SDL_assert(err == VK_SUCCESS);
    } else {
      SDL_assert(err == VK_SUCCESS);
    }

    TracyCZoneEnd(present_ctx);
  }
}

int32_t render_thread(void *data) {
  RenderThread *thread = (RenderThread *)data;

  // Init
  TB_CHECK_RETURN(init_render_thread(thread), "Failed to init render thread",
                  -1);
  TracyCSetThreadName("Render Thread");

  SDL_SemPost(thread->initialized);

  // Main thread loop
  while (true) {
    TracyCZoneN(ctx, "Render Frame", true);
    TracyCZoneColor(ctx, TracyCategoryColorRendering);
    FrameState *frame_state = &thread->frame_states[thread->frame_idx];

    // Wait for signal
    {
      TracyCZoneN(wait_ctx, "Wait for Main Thread", true);
      TracyCZoneColor(wait_ctx, TracyCategoryColorWait);

      SDL_SemWait(frame_state->wait_sem);

      TracyCZoneEnd(wait_ctx);
    }

    // If we got a stop signal, stop
    if (thread->stop_signal > 0) {
      TracyCZoneEnd(ctx);
      break;
    }

    tick_render_thread(thread, &thread->frame_states[thread->frame_idx]);

    // Increment frame count when done
    thread->frame_count++;
    thread->frame_idx = thread->frame_count % TB_MAX_FRAME_STATES;

    // Signal frame done
    SDL_SemPost(frame_state->signal_sem);

    TracyCZoneEnd(ctx);
  }

  // Frame states must be destroyed on this thread
  destroy_frame_states(thread->device, thread->vma_alloc, &thread->vk_alloc,
                       thread->frame_states);

  return 0;
}
