#include "tb_render_thread.h"

#include "mimalloc.h"
#include <stdbool.h>

#include "tb_allocator.h"
#include "tb_profiling.h"

#include "tb_common.h"
#include "tb_engine_config.h"
#include "tb_log.h"
#include "tb_sdl.h"
#include "tb_vk.h"
#include "tb_vk_alloc.h"
#include "tb_vk_dbg.h"
#include "tb_vma.h"

// we know that rendersystem.c defines this
extern bool try_map(VmaAllocator vma, VmaAllocation alloc, void **ptr);

int32_t render_thread(void *data);

// Public API

bool tb_start_render_thread(TbRenderThreadDescriptor *desc,
                            TbRenderThread *thread) {
  TB_CHECK_RETURN(desc, "Invalid TbRenderThreadDescriptor", false);
  thread->window = desc->window;
  thread->initialized = SDL_CreateSemaphore(0);
  thread->resized = SDL_CreateSemaphore(0);
  thread->thread = SDL_CreateThread(render_thread, "Render Thread", thread);
  TB_CHECK_RETURN(thread->thread, "Failed to create render thread", false);
  return true;
}

void tb_signal_render(TbRenderThread *thread, uint32_t frame_idx) {
  TB_CHECK(frame_idx < TB_MAX_FRAME_STATES, "Invalid frame index");
  SDL_PostSemaphore(thread->frame_states[frame_idx].wait_sem);
  // TB_LOG_DEBUG(TB_LOG_CATEGORY_RENDER_THREAD,
  //              "+++Signaled render thread frame %d+++", frame_idx);
}

void tb_wait_render(TbRenderThread *thread, uint32_t frame_idx) {
  // TB_LOG_DEBUG(TB_LOG_CATEGORY_RENDER_THREAD,
  //              "~~~Waiting for GPU to frame %d~~~", frame_idx);
  TB_CHECK(frame_idx < TB_MAX_FRAME_STATES, "Invalid frame index");
  SDL_WaitSemaphore(thread->frame_states[frame_idx].signal_sem);
  TracyCZoneNC(gpu_ctx, "Wait for GPU", TracyCategoryColorWait, true);
  vkWaitForFences(thread->device, 1, &thread->frame_states[frame_idx].fence,
                  VK_TRUE, SDL_MAX_UINT64);
  // TB_LOG_DEBUG(TB_LOG_CATEGORY_RENDER_THREAD,
  //              "---Done waiting for GPU on frame %d---", frame_idx);
  TracyCZoneEnd(gpu_ctx);
}

void tb_wait_thread_initialized(TbRenderThread *thread) {
  TracyCZoneNC(ctx, "Wait Render Thread Initialize", TracyCategoryColorWait,
               true);
  // TB_LOG_DEBUG(TB_LOG_CATEGORY_RENDER_THREAD, "%s",
  //              "Waiting for render thread to initialize");
  SDL_WaitSemaphore(thread->initialized);
  TracyCZoneEnd(ctx);
}

void tb_stop_render_thread(TbRenderThread *thread) {
  uint32_t frame_idx = thread->frame_idx;
  // Set the stop signal
  thread->stop_signal = 1;
  // Signal Render thread
  tb_signal_render(thread, frame_idx);

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
                          TbFrameState *states);

void tb_destroy_render_thread(TbRenderThread *thread) {
  TB_CHECK(thread, "Invalid thread");

  const VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;
  TbAllocator gp_alloc = thread->gp_alloc.alloc;

  vmaDestroyAllocator(thread->vma_alloc);

  vkDestroyDevice(thread->device, vk_alloc);

  tb_free(gp_alloc, thread->queue_props);

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

  *thread = (TbRenderThread){0};
}

// Private internals

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifdef VALIDATION
static bool check_layer(const char *check_name, uint32_t layer_count,
                        VkLayerProperties *layers) {
  bool found = false;
  for (uint32_t i = 0; i < layer_count; i++) {
    if (!SDL_strcmp(check_name, layers[i].layerName)) {
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
  (void)pCallbackData;

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    TB_LOG_VERBOSE(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    TB_LOG_INFO(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity &
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    TB_LOG_WARN(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    TB_LOG_ERROR(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else {
    TB_LOG_DEBUG(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  }

  // Helper for breaking when encountering a non-info message
  if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    SDL_TriggerBreakpoint();
  }

  return false;
}
#endif

bool init_instance(TbAllocator tmp_alloc, const VkAllocationCallbacks *vk_alloc,
                   VkInstance *instance) {
  TracyCZoneN(ctx, "Initialize Instance", true);
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
    {
      tb_auto sdl_ext_names = SDL_Vulkan_GetInstanceExtensions(&ext_count);
      for (uint32_t i = 0; i < ext_count; ++i) {
        ext_names[i] = sdl_ext_names[i];
      }
    }

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
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = TB_ENGINE_NAME;
    app_info.engineVersion =
        VK_MAKE_VERSION(TB_ENGINE_VERSION_MAJOR, TB_ENGINE_VERSION_MINOR,
                        TB_ENGINE_VERSION_PATCH);
    app_info.apiVersion = VK_API_VERSION_1_3;

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

  TracyCZoneEnd(ctx);
  return true;
}

bool init_debug_messenger(VkInstance instance,
                          const VkAllocationCallbacks *vk_alloc,
                          VkDebugUtilsMessengerEXT *debug) {
  TracyCZoneN(ctx, "Initialize Debug Messenger", true);
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
#else
  (void)instance;
  (void)vk_alloc;
  (void)debug;
#endif
  TracyCZoneEnd(ctx);
  return true;
}

VkResult create_semaphore(VkDevice device,
                          const VkAllocationCallbacks *vk_alloc,
                          const char *name, VkSemaphore *sem) {
  VkSemaphoreCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  VkResult err = vkCreateSemaphore(device, &create_info, vk_alloc, sem);
  TB_VK_CHECK_RET(err, "Failed to create semaphore", err);
  SET_VK_NAME(device, *sem, VK_OBJECT_TYPE_SEMAPHORE, name);
  return err;
}

bool init_frame_states(VkPhysicalDevice gpu, VkDevice device,
                       const TbSwapchain *swapchain,
                       uint32_t graphics_queue_family_index,
                       VmaAllocator vma_alloc,
                       const VkAllocationCallbacks *vk_alloc,
                       TbFrameState *states) {
  TracyCZoneN(ctx, "Initialize Frame States", true);
  TB_CHECK_RETURN(states, "Invalid states", false);
  VkResult err = VK_SUCCESS;

  uint32_t swap_img_count = 0;
  err = vkGetSwapchainImagesKHR(device, swapchain->swapchain, &swap_img_count,
                                NULL);
  TB_VK_CHECK_RET(err, "Failed to get swapchain image count", false);
  TB_CHECK_RETURN(swap_img_count >= TB_MAX_FRAME_STATES,
                  "Fewer than required swapchain images", false);
  if (swap_img_count > TB_MAX_FRAME_STATES) {
    swap_img_count = TB_MAX_FRAME_STATES;
  }

  VkImage swapchain_images[TB_MAX_FRAME_STATES] = {0};
  err = vkGetSwapchainImagesKHR(device, swapchain->swapchain, &swap_img_count,
                                swapchain_images);
  // HACK: Android can often want more than TB_MAX_FRAME_STATE images which
  // means we get VK_INCOMPLETE back. Should we handle the case where the number
  // of frames in the swapchain is different than the frames in flight?
  TB_VK_CHECK_RET(err != VK_SUCCESS && err != VK_INCOMPLETE,
                  "Failed to get swapchain images", false);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    TbFrameState *state = &states[i];

    tb_create_arena_alloc("Render Thread Frame State Tmp Alloc",
                          &state->tmp_alloc, 128 * 1024 * 1024);

    state->wait_sem = SDL_CreateSemaphore(0);
    TB_CHECK_RETURN(state->wait_sem,
                    "Failed to create frame state wait semaphore", false);
    state->signal_sem = SDL_CreateSemaphore(1);
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
          .commandBufferCount = 2,
          .commandPool = state->command_pool,
      };
      err = vkAllocateCommandBuffers(device, &alloc_info,
                                     state->base_command_buffers);
      TB_VK_CHECK_RET(err, "Failed to create frame state command buffer",
                      false);
      SET_VK_NAME(device, state->base_command_buffers[0],
                  VK_OBJECT_TYPE_COMMAND_BUFFER, "Starting Command Buffer");
      SET_VK_NAME(device, state->base_command_buffers[1],
                  VK_OBJECT_TYPE_COMMAND_BUFFER, "Ending Command Buffer");
    }

    {
      state->swapchain_image = swapchain_images[i];
      SET_VK_NAME(device, state->swapchain_image, VK_OBJECT_TYPE_IMAGE,
                  "Frame State TbSwapchain Image");
    }

    create_semaphore(device, vk_alloc,
                     "FrameState TbSwapchain Image Acquired Sem",
                     &state->img_acquired_sem);
    create_semaphore(device, vk_alloc, "Frame State TbSwapchain Image Sem",
                     &state->swapchain_image_sem);
    create_semaphore(device, vk_alloc, "Frame State Upload Complete Sem",
                     &state->upload_complete_sem);
    create_semaphore(device, vk_alloc, "Frame State Render Complete Sem",
                     &state->render_complete_sem);
    create_semaphore(device, vk_alloc, "Frame State Frame Complete Sem",
                     &state->frame_complete_sem);

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
      TracyCGPUContext *gpu_ctx = TracyCVkContextHostCalib(
          gpu, device, vkResetQueryPool,
          vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
          vkGetCalibratedTimestampsEXT);
      const char *name = "Frame State GPU Context";
      TracyCVkContextName(gpu_ctx, name, SDL_strlen(name));
      state->tracy_gpu_context = gpu_ctx;
    }

    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = TB_VMA_TMP_GPU_MB * 1024 * 1024,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
      };
      VmaAllocationCreateInfo alloc_create_info = {
          .usage = VMA_MEMORY_USAGE_AUTO,
          .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
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

  TracyCZoneEnd(ctx);
  return true;
}

void destroy_frame_states(VkDevice device, VmaAllocator vma_alloc,
                          const VkAllocationCallbacks *vk_alloc,
                          TbFrameState *states) {
  TB_CHECK(states, "Invalid states");

  vkDeviceWaitIdle(device);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    TbFrameState *state = &states[i];

    tb_destroy_arena_alloc(state->tmp_alloc);

    SDL_DestroySemaphore(state->wait_sem);
    SDL_DestroySemaphore(state->signal_sem);

    vkFreeCommandBuffers(device, state->command_pool, 2,
                         state->base_command_buffers);
    if (state->pass_command_buffer_count) {
      vkFreeCommandBuffers(device, state->command_pool,
                           state->pass_command_buffer_count,
                           state->pass_command_buffers);
    }
    vkDestroyCommandPool(device, state->command_pool, vk_alloc);

    TracyCVkContextDestroy(state->tracy_gpu_context);

    // swapchain_image is owned by the KHR swapchain and doesn't need to be
    // freed

    vkDestroySemaphore(device, state->img_acquired_sem, vk_alloc);
    vkDestroySemaphore(device, state->swapchain_image_sem, vk_alloc);
    vkDestroySemaphore(device, state->upload_complete_sem, vk_alloc);
    vkDestroySemaphore(device, state->render_complete_sem, vk_alloc);
    vkDestroySemaphore(device, state->frame_complete_sem, vk_alloc);

    vkDestroyFence(device, state->fence, vk_alloc);

    vmaDestroyBuffer(vma_alloc, state->tmp_gpu_buffer, state->tmp_gpu_alloc);

    *state = (TbFrameState){0};
  }
}

bool init_gpu(VkInstance instance, TbAllocator gp_alloc, TbAllocator tmp_alloc,
              VkPhysicalDevice *gpu, VkPhysicalDeviceProperties2 *gpu_props,
              VkPhysicalDeviceDriverProperties *driver_props,
              VkPhysicalDeviceDescriptorBufferPropertiesEXT *desc_buf_props,
              uint32_t *queue_family_count,
              VkQueueFamilyProperties **queue_props,
              VkPhysicalDeviceFeatures *gpu_features,
              VkPhysicalDeviceMemoryProperties *gpu_mem_props) {
  TracyCZoneN(ctx, "Initialize GPU", true);
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
  *desc_buf_props = (VkPhysicalDeviceDescriptorBufferPropertiesEXT){
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
  };
  *driver_props = (VkPhysicalDeviceDriverProperties){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
      .pNext = desc_buf_props,
  };
  *gpu_props = (VkPhysicalDeviceProperties2){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = driver_props,
  };
  vkGetPhysicalDeviceProperties2(*gpu, gpu_props);

  vkGetPhysicalDeviceQueueFamilyProperties(*gpu, queue_family_count, NULL);

  (*queue_props) =
      tb_alloc_nm_tp(gp_alloc, *queue_family_count, VkQueueFamilyProperties);
  TB_CHECK_RETURN(queue_props, "Failed to allocate queue props", false);
  vkGetPhysicalDeviceQueueFamilyProperties(*gpu, queue_family_count,
                                           (*queue_props));

  vkGetPhysicalDeviceFeatures(*gpu, gpu_features);

  vkGetPhysicalDeviceMemoryProperties(*gpu, gpu_mem_props);

  VkFormatProperties props = {0};
  vkGetPhysicalDeviceFormatProperties(*gpu, VK_FORMAT_R16G16B16A16_SINT,
                                      &props);

  TracyCZoneEnd(ctx);
  return true;
}

bool find_queue_families(TbAllocator tmp_alloc, VkPhysicalDevice gpu,
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
    TB_LOG_INFO(SDL_LOG_CATEGORY_RENDER, "Loading required extension: %s",
                ext_name);
    out_ext_names[(*out_ext_count)++] = ext_name;
    return;
  }

  TB_LOG_ERROR(SDL_LOG_CATEGORY_RENDER, "Missing required extension: %s",
               ext_name);
  SDL_TriggerBreakpoint();
}

bool optional_device_ext(const char **out_ext_names, uint32_t *out_ext_count,
                         const VkExtensionProperties *props,
                         uint32_t prop_count, const char *ext_name) {
  if (device_supports_ext(props, prop_count, ext_name)) {
    SDL_assert((*out_ext_count + 1) < MAX_EXT_COUNT);
    TB_LOG_INFO(SDL_LOG_CATEGORY_RENDER, "Loading optional extension: %s",
                ext_name);
    out_ext_names[(*out_ext_count)++] = ext_name;
    return true;
  }

  TB_LOG_WARN(SDL_LOG_CATEGORY_RENDER, "Optional extension not supported: %s",
              ext_name);
  return false;
}

bool init_device(VkPhysicalDevice gpu, uint32_t graphics_queue_family_index,
                 uint32_t present_queue_family_index, TbAllocator tmp_alloc,
                 const VkAllocationCallbacks *vk_alloc,
                 TbRenderExtensionSupport *ext_support, VkDevice *device) {
  VkResult err = VK_SUCCESS;

  uint32_t device_ext_count = 0;
  const char *device_ext_names[MAX_EXT_COUNT] = {0};

  TbRenderExtensionSupport ext = {0};
  {
    const uint32_t max_props = 256;
    VkExtensionProperties *props =
        tb_alloc_nm_tp(tmp_alloc, max_props, VkExtensionProperties);

    uint32_t prop_count = 0;
    err = vkEnumerateDeviceExtensionProperties(gpu, NULL, &prop_count, NULL);
    TB_CHECK_RETURN(err == VK_SUCCESS,
                    "Failed to enumerate device extension property count",
                    false);

    TB_CHECK_RETURN(prop_count < max_props,
                    "Device extension property count out of range", false);

    err = vkEnumerateDeviceExtensionProperties(gpu, NULL, &prop_count, props);
    TB_CHECK_RETURN(err == VK_SUCCESS,
                    "Failed to enumerate device extension properties", false);

    // Only need portability on macos / ios
#if (defined(VK_USE_PLATFORM_MACOS_MVK) ||                                     \
     defined(VK_USE_PLATFORM_IOS_MVK)) &&                                      \
    (VK_HEADER_VERSION >= 216)
    ext.portability = required_device_ext(
        &device_ext_names, &device_ext_count, props, prop_count,
        VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    // Need a swapchain
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Required for null descriptors
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);

    // Required for relaxed vertex shader writes not being used by some frag
    // permutations
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count, VK_KHR_MAINTENANCE_4_EXTENSION_NAME);

    // Required for spirv 1.4
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count,
                        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);

    // Required for mesh shader
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count, VK_KHR_SPIRV_1_4_EXTENSION_NAME);

    // Mesh Shader support
    // required_device_ext((const char **)&device_ext_names, &device_ext_count,
    //                    props, prop_count, VK_EXT_MESH_SHADER_EXTENSION_NAME);

    // We want to use descriptor buffers
    required_device_ext((const char **)&device_ext_names, &device_ext_count,
                        props, prop_count,
                        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);

#ifdef TRACY_ENABLE
    // Enable calibrated timestamps if we can when profiling with tracy
    {
      ext.calibrated_timestamps = optional_device_ext(
          (const char **)&device_ext_names, &device_ext_count, props,
          prop_count, VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    }
#endif
  }

  VkPhysicalDeviceRobustness2FeaturesEXT vk_rob2_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
      .nullDescriptor = VK_TRUE,
  };

  VkPhysicalDeviceVulkan13Features vk_13_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &vk_rob2_features,
      .dynamicRendering = VK_TRUE,
      .shaderDemoteToHelperInvocation = VK_TRUE,
      .maintenance4 = VK_TRUE,
  };

  VkPhysicalDeviceVulkan12Features vk_12_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &vk_13_features,
      .hostQueryReset = VK_TRUE,
      .shaderFloat16 = VK_TRUE,
      .descriptorIndexing = VK_TRUE,
      .descriptorBindingVariableDescriptorCount = VK_TRUE,
      .descriptorBindingPartiallyBound = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
      .runtimeDescriptorArray = VK_TRUE,
      .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
      .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
      .shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE,
      .drawIndirectCount = VK_TRUE,
  };

  VkPhysicalDeviceVulkan11Features vk_11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &vk_12_features,
      .multiview = VK_TRUE,
      .storageBuffer16BitAccess = VK_TRUE,
      .shaderDrawParameters = VK_TRUE,
  };

  // fragmentStoresAndAtomics Must enable to avoid false positive in validation
  // layers Should be fixed by this PR:
  // https://github.com/KhronosGroup/Vulkan-ValidationLayers/pull/7393
  VkPhysicalDeviceFeatures vk_features = {
      .samplerAnisotropy = VK_TRUE,
      .depthClamp = VK_TRUE,
      .shaderInt16 = VK_TRUE,
      .vertexPipelineStoresAndAtomics = VK_TRUE,
      .fragmentStoresAndAtomics = VK_TRUE,
      .multiDrawIndirect = VK_TRUE,
      .shaderImageGatherExtended = VK_TRUE,
  };

  float queue_priorities[1] = {0.0};
  uint32_t queue_count = 1;
  VkDeviceQueueCreateInfo queues[2] = {{0}, {0}};
  queues[0] = (VkDeviceQueueCreateInfo){
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .queueFamilyIndex = graphics_queue_family_index,
      .queueCount = 1,
      .pQueuePriorities = queue_priorities,
      .flags = 0,
  };
  if (present_queue_family_index != graphics_queue_family_index) {
    queues[1] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .queueFamilyIndex = present_queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = queue_priorities,
        .flags = 0,
    };
    queue_count++;
  }
  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = (const void *)&vk_11_features,
      .queueCreateInfoCount = queue_count,
      .pQueueCreateInfos = queues,
      .enabledExtensionCount = device_ext_count,
      .ppEnabledExtensionNames = device_ext_names,
      .pEnabledFeatures = &vk_features,
  };

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
      .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
      .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
      .vkBindBufferMemory2KHR = vkBindBufferMemory2,
      .vkBindImageMemory2KHR = vkBindImageMemory2,
      .vkGetPhysicalDeviceMemoryProperties2KHR =
          vkGetPhysicalDeviceMemoryProperties2,
      .vkGetDeviceBufferMemoryRequirements =
          vkGetDeviceBufferMemoryRequirements,
      .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
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
      .vulkanApiVersion = VK_API_VERSION_1_3,
      .pAllocationCallbacks = vk_alloc,
      .pDeviceMemoryCallbacks = &vma_callbacks,
  };

  VkResult err = vmaCreateAllocator(&create_info, vma_alloc);
  TB_VK_CHECK_RET(err, "Failed to create vma allocator", false);
  return true;
}

bool init_swapchain(SDL_Window *window, VkDevice device, VkPhysicalDevice gpu,
                    VkSurfaceKHR surface, TbAllocator tmp_alloc,
                    const VkAllocationCallbacks *vk_alloc,
                    TbSwapchain *swapchain) {
  int32_t width = 0;
  int32_t height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);

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
  SET_VK_NAME(device, vk_swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR,
              "TbSwapchain");

  *swapchain = (TbSwapchain){
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

bool init_render_thread(TbRenderThread *thread) {
  TracyCZoneNC(ctx, "Render Thread Init", TracyCategoryColorRendering, true);
  TB_CHECK_RETURN(thread, "Invalid render thread", false);
  TB_CHECK_RETURN(thread->window, "Render thread given no window", false);

  // Create renderer allocators
  {
    TracyCZoneN(ctx, "Initialize Render Thread Allocators", true);
    const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
    tb_create_arena_alloc("Render Arena", &thread->render_arena,
                          arena_alloc_size);

    tb_create_gen_alloc(&thread->gp_alloc, "Render Std Alloc");
    TracyCZoneEnd(ctx);
  }

  tb_auto fn = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
  {
    TracyCZoneN(ctx, "Initialize Volk", true);
    volkInitializeCustom(fn);
    TracyCZoneEnd(ctx);
  }

  // Create vulkan allocator
  thread->vk_alloc = (VkAllocationCallbacks){
      .pfnAllocation = tb_vk_alloc_fn,
      .pfnReallocation = tb_vk_realloc_fn,
      .pfnFree = tb_vk_free_fn,
  };

  TbAllocator gp_alloc = thread->gp_alloc.alloc;
  TbAllocator tmp_alloc = thread->render_arena.alloc;
  const VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;

  TB_CHECK_RETURN(init_instance(tmp_alloc, vk_alloc, &thread->instance),
                  "Failed to create instance", false);

  TB_CHECK_RETURN(init_debug_messenger(thread->instance, vk_alloc,
                                       &thread->debug_utils_messenger),
                  "Failed to create debug messenger", false);

  TB_CHECK_RETURN(init_gpu(thread->instance, gp_alloc, tmp_alloc, &thread->gpu,
                           &thread->gpu_props, &thread->driver_props,
                           &thread->desc_buf_props, &thread->queue_family_count,
                           &thread->queue_props, &thread->gpu_features,
                           &thread->gpu_mem_props),
                  "Failed to select gpu", false)

  TB_CHECK_RETURN(SDL_Vulkan_CreateSurface(thread->window, thread->instance,
                                           vk_alloc, &thread->surface),
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

  TB_CHECK_RETURN(
      init_frame_states(thread->gpu, thread->device, &thread->swapchain,
                        thread->graphics_queue_family_index, thread->vma_alloc,
                        vk_alloc, thread->frame_states),
      "Failed to init frame states", false);

  TracyCZoneEnd(ctx);
  return true;
}

void resize_swapchain(TbRenderThread *thread) {
  VkSwapchainKHR old_swapchain = thread->swapchain.swapchain;

  TbAllocator tmp_alloc = thread->render_arena.alloc;
  const VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;
  TB_CHECK(init_swapchain(thread->window, thread->device, thread->gpu,
                          thread->surface, tmp_alloc, vk_alloc,
                          &thread->swapchain),
           "Failed to resize swapchain");

  VkImage swapchain_images[TB_MAX_FRAME_STATES] = {0};
  VkResult err =
      vkGetSwapchainImagesKHR(thread->device, thread->swapchain.swapchain,
                              &thread->swapchain.image_count, swapchain_images);
  TB_VK_CHECK(err, "Failed to get swapchain images");

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    TbFrameState *state = &thread->frame_states[i];
    state->swapchain_image = swapchain_images[i];
    SET_VK_NAME(thread->device, state->swapchain_image, VK_OBJECT_TYPE_IMAGE,
                "Frame State TbSwapchain Image");
  }

  // Notify the main thread that the swapchain has been resized
  thread->swapchain_resize_signal = 1;

  vkDestroySwapchainKHR(thread->device, old_swapchain, vk_alloc);
}

void *record_pass_begin(VkCommandBuffer buffer, TracyCGPUContext *ctx,
                        TbPassContext *pass) {
  (void)ctx;
  void *ret = NULL;
#ifdef TRACY_ENABLE
  cmd_begin_label(buffer, pass->label, (float4){1.0f, 1.0f, 0.5f, 1.0f});
  // TracyCVkNamedZone(ctx, frame_scope, buffer, pass->label, 2, true);
  // ret = frame_scope;
#endif

  // Perform any necessary image transitions
  for (uint32_t i = 0; i < pass->barrier_count; ++i) {
    const TbImageTransition *barrier = &pass->barriers[i];
    vkCmdPipelineBarrier(buffer, barrier->src_flags, barrier->dst_flags, 0, 0,
                         NULL, 0, NULL, 1, &barrier->barrier);
  }

  // Assume a pass with no attachments is doing compute work
  if (pass->attachment_count > 0) {
    vkCmdBeginRendering(buffer, pass->render_info);
  }
  return ret;
}

void record_pass_end(VkCommandBuffer buffer, TracyCGPUScope *scope,
                     TbPassContext *pass) {
  (void)scope;
  // TracyCVkZoneEnd(scope);
  //  Assume a pass with no attachments has done no rendering
  if (pass->attachment_count > 0) {
    vkCmdEndRendering(buffer);
  }
}

void tick_render_thread(TbRenderThread *thread, TbFrameState *state) {
  VkResult err = VK_SUCCESS;

  VkDevice device = thread->device;

  VkQueue graphics_queue = thread->graphics_queue;
  VkQueue present_queue = thread->present_queue;

  VkSemaphore img_acquired_sem = state->img_acquired_sem;
  VkSemaphore upload_complete_sem = state->upload_complete_sem;
  VkSemaphore render_complete_sem = state->render_complete_sem;
  VkSemaphore frame_complete_sem = state->frame_complete_sem;
  VkSemaphore swapchain_image_sem = state->swapchain_image_sem;
  VkFence fence = state->fence;

  VkCommandBuffer start_buffer = state->base_command_buffers[0];
  VkCommandBuffer end_buffer = state->base_command_buffers[1];

  // Ensure the frame state we're about to use isn't being handled by the GPU
  {
    TracyCZoneN(fence_ctx, "Wait for GPU", true);
    TracyCZoneColor(fence_ctx, TracyCategoryColorWait);
    VkResult res = VK_TIMEOUT;
    while (res != VK_SUCCESS) {
      if (res == VK_TIMEOUT) {
        res =
            vkWaitForFences(thread->device, 1, &fence, VK_TRUE, SDL_MAX_UINT64);
      } else {
        TB_CHECK(false, "Error waiting for fence");
      }
    }

    TracyCZoneEnd(fence_ctx);

    vkResetFences(device, 1, &fence);
  }

  // Acquire Image
  {
    TracyCZoneN(acquire_ctx, "Acquired Next TbSwapchain Image", true);
    do {
      uint32_t idx = 0xFFFFFFFF;
      err = vkAcquireNextImageKHR(device, thread->swapchain.swapchain,
                                  SDL_MIN_UINT64, img_acquired_sem,
                                  VK_NULL_HANDLE, &idx);
      thread->frame_idx = idx;
      if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        // swapchain is out of date (e.g. the window was resized) and
        // must be recreated:
        resize_swapchain(thread);
      } else if (err == VK_SUBOPTIMAL_KHR) {
        // swapchain is not as optimal as it could be, but the
        // platform's presentation engine will still present the image
        // correctly.
        break;
      } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
        // If the surface was lost we could re-create it.
        // But the surface is owned by SDL3
        SDL_assert(err == VK_SUCCESS);
      } else if (err == VK_NOT_READY) {
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

  // Write descriptor set updates at the top of the frame here

  {
    TracyCZoneN(desc_ctx, "Update Descriptors", true);
    TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
    uint32_t write_count = TB_DYN_ARR_SIZE(state->set_write_queue->storage);
    TB_DYN_ARR_RESET(writes, state->tmp_alloc.alloc, write_count);

    VkWriteDescriptorSet write = {0};
    while (TB_QUEUE_POP(*state->set_write_queue, &write)) {
      TB_DYN_ARR_APPEND(writes, write);
    }
    write_count = TB_DYN_ARR_SIZE(writes);

    vkUpdateDescriptorSets(device, write_count, writes.data, 0, NULL);
    TracyCZoneEnd(desc_ctx);
  }

  // Draw
  {
    TracyCZoneN(draw_ctx, "Draw", true);
    TracyCZoneColor(draw_ctx, TracyCategoryColorRendering);

    TracyCGPUContext *gpu_ctx = (TracyCGPUContext *)state->tracy_gpu_context;

    // Start Upload Record
    {
      VkCommandBufferBeginInfo begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      err = vkBeginCommandBuffer(start_buffer, &begin_info);
      TB_VK_CHECK(err, "Failed to begin command buffer");
    }

    // Upload
    {
      {
        TracyCZoneN(up_ctx, "Record Upload", true);
        TracyCVkNamedZone(gpu_ctx, upload_scope, start_buffer, "Upload", 1,
                          true);
        // Upload all buffer requests
        {
          TbBufferCopy up = {0};
          while (TB_QUEUE_POP(*state->buf_copy_queue, &up)) {
            vkCmdCopyBuffer(start_buffer, up.src, up.dst, 1, &up.region);
          }
        }

        // Upload all buffer to image requests
        {
          TbBufferImageCopy up = {0};
          while (TB_QUEUE_POP(*state->buf_img_copy_queue, &up)) {
            // Issue an upload command only if the src buffer exists
            // If it doesn't, assume that we want to only do a transition but
            // no copy
            if (up.src != VK_NULL_HANDLE) {
              VkImageMemoryBarrier barrier = {
                  .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                  .subresourceRange = up.range,
                  .srcAccessMask = 0,
                  .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                  .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  .image = up.dst,
              };
              vkCmdPipelineBarrier(start_buffer,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                                   0, NULL, 1, &barrier);

              // Perform the copy
              vkCmdCopyBufferToImage(start_buffer, up.src, up.dst,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                     &up.region);
            }

            // Transition to readable layout
            {
              VkImageMemoryBarrier barrier = {
                  .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                  .subresourceRange = up.range,
                  .srcAccessMask = 0,
                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                  .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  .image = up.dst,
              };
              vkCmdPipelineBarrier(start_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                   NULL, 0, NULL, 1, &barrier);
            }
          }
        }

        TracyCVkZoneEnd(upload_scope);
        TracyCZoneEnd(up_ctx);
      }

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

        vkCmdPipelineBarrier(start_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1, &barrier);
        TracyCZoneEnd(swap_trans_e);
      }
    }

    // End upload record
    {
      err = vkEndCommandBuffer(start_buffer);
      TB_VK_CHECK(err, "Failed to end upload command buffer");
    }

    // Submit upload work
    {
      TracyCZoneN(submit_ctx, "Submit Upload", true);
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
            .pCommandBuffers = &start_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &upload_complete_sem,
        };

        queue_begin_label(graphics_queue, "Upload",
                          (float4){1.0f, 0.1f, 0.1f, 1.0f});
        err = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        queue_end_label(graphics_queue);
        TB_VK_CHECK(err, "Failed to submit upload work");
      }

      TracyCZoneEnd(submit_ctx);
    }

    // Record registered passes
    VkSemaphore pre_final_sem = upload_complete_sem;
    if (TB_DYN_ARR_SIZE(state->pass_contexts) > 0) {
      TracyCZoneN(pass_ctx, "Record Passes", true);
      pre_final_sem = render_complete_sem;
      uint32_t last_pass_buffer_idx = 0xFFFFFFFF;
      TB_DYN_ARR_FOREACH(state->pass_contexts, pass_idx) {
        TbPassContext *pass = &TB_DYN_ARR_AT(state->pass_contexts, pass_idx);

        VkCommandBuffer pass_buffer =
            state->pass_command_buffers[pass->command_buffer_index];
        if (pass->command_buffer_index != last_pass_buffer_idx) {
          // Submit previous command buffer
          if (last_pass_buffer_idx != 0xFFFFFFFF) {
            vkEndCommandBuffer(
                state->pass_command_buffers[last_pass_buffer_idx]);

            // Submit pass work
            {
              TracyCZoneN(submit_ctx, "Submit Passes", true);
              TracyCZoneColor(submit_ctx, TracyCategoryColorRendering);

              uint32_t wait_sem_count = 0;
              VkSemaphore wait_sems[16] = {0};
              VkPipelineStageFlags wait_stage_flags[16] = {0};

              if (last_pass_buffer_idx == 0) {
                wait_sems[wait_sem_count] = upload_complete_sem;
                wait_stage_flags[wait_sem_count++] =
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
              }

              {
                VkSubmitInfo submit_info = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .waitSemaphoreCount = wait_sem_count,
                    .pWaitSemaphores = wait_sems,
                    .pWaitDstStageMask = wait_stage_flags,
                    .commandBufferCount = 1,
                    .pCommandBuffers =
                        &state->pass_command_buffers[last_pass_buffer_idx],
                };

                err = vkQueueSubmit(graphics_queue, 1, &submit_info,
                                    VK_NULL_HANDLE);
                TB_VK_CHECK(err, "Failed to submit pass work");
              }

              TracyCZoneEnd(submit_ctx);
            }
          }

          VkCommandBufferBeginInfo begin_info = {
              .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          };
          vkBeginCommandBuffer(pass_buffer, &begin_info);
        }

        void *pass_scope = record_pass_begin(pass_buffer, gpu_ctx, pass);
        if (pass->attachment_count > 0) {
          TB_DYN_ARR_FOREACH(state->draw_contexts, draw_idx) {
            TbDrawContext *draw =
                &TB_DYN_ARR_AT(state->draw_contexts, draw_idx);
            if (draw->pass_id == pass->id && draw->batch_count > 0) {
              draw->record_fn(gpu_ctx, pass_buffer, draw->batch_count,
                              draw->batches);
            }
          }
        } else {
          TB_DYN_ARR_FOREACH(state->dispatch_contexts, disp_idx) {
            TbDispatchContext *dispatch =
                &TB_DYN_ARR_AT(state->dispatch_contexts, disp_idx);
            if (dispatch->pass_id == pass->id && dispatch->batch_count > 0) {
              dispatch->record_fn(gpu_ctx, pass_buffer, dispatch->batch_count,
                                  dispatch->batches);
            }
          }
        }
        record_pass_end(pass_buffer, pass_scope, pass);

#ifdef TRACY_ENABLE
        cmd_end_label(pass_buffer);
#endif

        last_pass_buffer_idx = pass->command_buffer_index;
      }
      vkEndCommandBuffer(state->pass_command_buffers[last_pass_buffer_idx]);

      // Submit last pass work
      {
        TracyCZoneN(submit_ctx, "Submit Passes", true);
        TracyCZoneColor(submit_ctx, TracyCategoryColorRendering);

        uint32_t wait_sem_count = 0;
        VkSemaphore wait_sems[16] = {0};
        VkPipelineStageFlags wait_stage_flags[16] = {0};

        {
          VkSubmitInfo submit_info = {
              .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
              .waitSemaphoreCount = wait_sem_count,
              .pWaitSemaphores = wait_sems,
              .pWaitDstStageMask = wait_stage_flags,
              .commandBufferCount = 1,
              .pCommandBuffers =
                  &state->pass_command_buffers[last_pass_buffer_idx],
              .signalSemaphoreCount = 1,
              .pSignalSemaphores = &render_complete_sem,
          };

          err = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
          TB_VK_CHECK(err, "Failed to submit pass work");
        }

        TracyCZoneEnd(submit_ctx);
      }

      TracyCZoneEnd(pass_ctx);
    }

    // Record finalization work
    {
      VkCommandBufferBeginInfo begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      err = vkBeginCommandBuffer(end_buffer, &begin_info);
      TB_VK_CHECK(err, "Failed to begin command buffer");
      TracyCVkCollect(gpu_ctx, end_buffer);

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
          end_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

      vkEndCommandBuffer(end_buffer);

      TracyCZoneEnd(swap_trans_e);
    }

    // Submit finalization work
    {
      TracyCZoneN(submit_ctx, "Submit Ending", true);
      TracyCZoneColor(submit_ctx, TracyCategoryColorRendering);

      uint32_t wait_sem_count = 0;
      VkSemaphore wait_sems[16] = {0};
      VkPipelineStageFlags wait_stage_flags[16] = {0};

      wait_sems[wait_sem_count] = pre_final_sem;
      wait_stage_flags[wait_sem_count++] =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

      {
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = wait_sem_count,
            .pWaitSemaphores = wait_sems,
            .pWaitDstStageMask = wait_stage_flags,
            .commandBufferCount = 1,
            .pCommandBuffers = &end_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &frame_complete_sem,
        };

        queue_begin_label(graphics_queue, "Finalize",
                          (float4){1.0f, 0.1f, 0.1f, 1.0f});
        err = vkQueueSubmit(graphics_queue, 1, &submit_info, state->fence);
        queue_end_label(graphics_queue);
        TB_VK_CHECK(err, "Failed to submit ending work");
      }

      TracyCZoneEnd(submit_ctx);
    }

    TracyCZoneEnd(draw_ctx);
  }

  // Present
  {
    TracyCZoneN(present_ctx, "Present", true);
    TracyCZoneColor(present_ctx, TracyCategoryColorRendering);

    VkSemaphore wait_sem = frame_complete_sem;
    if (thread->present_queue_family_index !=
        thread->graphics_queue_family_index) {
      // If we are using separate queues, change image ownership to the
      // present queue before presenting, waiting for the draw complete
      // semaphore and signalling the ownership released semaphore when
      // finished
      VkSubmitInfo submit_info = {
          .waitSemaphoreCount = 1,
          .pWaitSemaphores = &frame_complete_sem,
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
      resize_swapchain(thread);
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // TbSwapchain is not as optimal as it could be, but the
      // platform's presentation engine will still present the image
      // correctly.
    } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
      // If the surface was lost we could re-create it.
      // But the surface is owned by SDL3
      SDL_assert(err == VK_SUCCESS);
    } else {
      SDL_assert(err == VK_SUCCESS);
    }
    TracyCZoneEnd(present_ctx);

    TracyCFrameMark;
  }

  // It's now safe to reset the arenas
  state->tmp_alloc = tb_reset_arena(state->tmp_alloc, false);
  thread->render_arena = tb_reset_arena(thread->render_arena, false);
}

int32_t render_thread(void *data) {
  TbRenderThread *thread = (TbRenderThread *)data;

  // Init
  TB_CHECK_RETURN(init_render_thread(thread), "Failed to init render thread",
                  -1);
  TracyCSetThreadName("Render Thread");

  SDL_PostSemaphore(thread->initialized);

  // Main thread loop
  while (true) {
    TracyCZone(ctx, true);
    {
      char frame_name[100] = {0};
      SDL_snprintf(frame_name, 100, "Render Frame %d", thread->frame_idx);
      TracyCZoneName(ctx, frame_name, SDL_strlen(frame_name));
    }
    TracyCZoneColor(ctx, TracyCategoryColorRendering);

    // If we got a stop signal, stop
    if (thread->stop_signal > 0) {
      TracyCZoneEnd(ctx);
      break;
    }

    // If the swapchain was resized, wait for the main thread to report that
    // it's all done handling the resize
    if (thread->swapchain_resize_signal) {
      TracyCZoneNC(resize_ctx, "Resize", TracyCategoryColorWait, true);
      SDL_WaitSemaphore(thread->resized);

      // Signal frame done before resetting frame_idx
      TbFrameState *frame_state = &thread->frame_states[thread->frame_idx];
      SDL_PostSemaphore(frame_state->signal_sem);

      thread->frame_count = 0;
      thread->frame_idx = 0;

      // Reset all frame state semaphores
      for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
        TbFrameState *state = &thread->frame_states[i];
        SDL_DestroySemaphore(state->wait_sem);
        SDL_DestroySemaphore(state->signal_sem);
        state->wait_sem = SDL_CreateSemaphore(0);
        state->signal_sem = SDL_CreateSemaphore(1);
      }

      thread->swapchain_resize_signal = 0;
      TracyCZoneEnd(resize_ctx);
    } else {
      {
        // Wait for signal from main thread that there's a frame ready to
        // process
        TbFrameState *frame_state = &thread->frame_states[thread->frame_idx];

        TracyCZoneN(wait_ctx, "Wait for Main Thread", true);
        TracyCZoneColor(wait_ctx, TracyCategoryColorWait);

        SDL_WaitSemaphore(frame_state->wait_sem);

        TracyCZoneEnd(wait_ctx);
      }

      TbFrameState *frame_state = &thread->frame_states[thread->frame_idx];
      tick_render_thread(thread, frame_state);

      // Signal frame done
      SDL_PostSemaphore(frame_state->signal_sem);

      // Increment frame count when done
      thread->frame_count++;
      thread->frame_idx = thread->frame_count % TB_MAX_FRAME_STATES;
    }

    TracyCZoneEnd(ctx);
  }

  // Frame states must be destroyed on this thread
  destroy_frame_states(thread->device, thread->vma_alloc, &thread->vk_alloc,
                       thread->frame_states);

  // Also must destroy swapchain and a few other primitives here
  vkDestroySwapchainKHR(thread->device, thread->swapchain.swapchain,
                        &thread->vk_alloc);

  return 0;
}
