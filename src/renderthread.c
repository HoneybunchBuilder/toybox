#include "renderthread.h"

#include "mimalloc.h"
#include <stdbool.h>

#include "allocator.h"
#include "profiling.h"

#include "config.h"
#include "tbcommon.h"
#include "tbsdl.h"
#include "tbvk.h"
#include "vk_mem_alloc.h"
#include "vkdbg.h"

// Public API

int32_t render_thread(void *data);

bool tb_start_render_thread(RenderThreadDescriptor *desc,
                            RenderThread *thread) {
  TB_CHECK_RETURN(desc, "Invalid RenderThreadDescriptor", false);
  thread->window = desc->window;
  thread->thread = SDL_CreateThread(render_thread, "Render Thread", thread);
  TB_CHECK_RETURN(thread->thread, "Failed to create render thread", false);
  return true;
}

void tb_signal_render(RenderThread *thread, uint32_t frame_idx) {
  TB_CHECK(frame_idx < MAX_FRAME_STATES, "Invalid frame index");
  SDL_SemPost(thread->frame_states[frame_idx].wait_sem);
}

void tb_wait_render(RenderThread *thread, uint32_t frame_idx) {
  TB_CHECK(frame_idx < MAX_FRAME_STATES, "Invalid frame index");
  SDL_SemWait(thread->frame_states[frame_idx].signal_sem);
}

void tb_stop_render_thread(RenderThread *thread) {
  (void)thread;
  // TODO: signal render thread to stop
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

static void *vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                         VkSystemAllocationScope scope) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  (void)scope;
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  void *ptr = mi_heap_malloc_aligned(heap, size, alignment);
  TracyCAllocN(ptr, size, "Vulkan");
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                           size_t alignment, VkSystemAllocationScope scope) {
  (void)scope;
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  TracyCFreeN(pOriginal, "Vulkan");
  void *ptr = mi_heap_realloc_aligned(heap, pOriginal, size, alignment);
  TracyCAllocN(ptr, size, "Vulkan");
  TracyCZoneEnd(ctx);
  return ptr;
}

static void vk_free_fn(void *pUserData, void *pMemory) {
  (void)pUserData;
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(pMemory, "Vulkan") mi_free(pMemory);
  TracyCZoneEnd(ctx);
}

bool init_instance(SDL_Window *window, Allocator tmp_alloc,
                   VkAllocationCallbacks *vk_alloc, VkInstance *instance) {
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

bool init_debug_messenger(VkInstance instance, VkAllocationCallbacks *vk_alloc,
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

bool init_frame_states(FrameState *states) {
  TB_CHECK_RETURN(states, "Invalid states", false);

  for (uint32_t i = 0; i < MAX_FRAME_STATES; ++i) {
    FrameState *state = &states[i];

    state->wait_sem = SDL_CreateSemaphore(1);
    TB_CHECK_RETURN(state->wait_sem,
                    "Failed to create frame state wait semaphore", false);
    state->signal_sem = SDL_CreateSemaphore(0);
    TB_CHECK_RETURN(state->signal_sem,
                    "Failed to create frame state signal semaphore", false);
  }

  return true;
}

void destroy_frame_states(FrameState *states) {
  TB_CHECK(states, "Invalid states");

  for (uint32_t i = 0; i < MAX_FRAME_STATES; ++i) {
    FrameState *state = &states[i];

    SDL_DestroySemaphore(state->wait_sem);
    SDL_DestroySemaphore(state->signal_sem);

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

void destroy_gpu(Allocator std_alloc, VkQueueFamilyProperties *queue_props) {
  tb_free(std_alloc, queue_props);
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
                 VkAllocationCallbacks *vk_alloc,
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

  VkDeviceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pNext = (const void *)&vk_11_features;
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

void vma_alloc_fn(VmaAllocator allocator, uint32_t memoryType,
                  VkDeviceMemory memory, VkDeviceSize size, void *pUserData) {
  (void)allocator;
  (void)memoryType;
  (void)memory;
  (void)size;
  (void)pUserData;
  TracyCAllocN((void *)memory, size, "VMA")
}
void vma_free_fn(VmaAllocator allocator, uint32_t memoryType,
                 VkDeviceMemory memory, VkDeviceSize size, void *pUserData) {
  (void)allocator;
  (void)memoryType;
  (void)memory;
  (void)size;
  (void)pUserData;
  TracyCFreeN((void *)memory, "VMA")
}

bool init_vma(VkInstance instance, VkPhysicalDevice gpu, VkDevice device,
              VkAllocationCallbacks *vk_alloc, VmaAllocator *vma_alloc) {
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
      vma_alloc_fn,
      vma_free_fn,
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
  thread->vk_heap = mi_heap_new();
  TB_CHECK_RETURN(thread->vk_heap, "Failed to create vk heap", false);

  thread->vk_alloc = (VkAllocationCallbacks){
      .pUserData = thread->vk_heap,
      .pfnAllocation = vk_alloc_fn,
      .pfnReallocation = vk_realloc_fn,
      .pfnFree = vk_free_fn,
  };

  Allocator std_alloc = thread->std_alloc.alloc;
  Allocator tmp_alloc = thread->render_arena.alloc;
  VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;

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

  TB_CHECK_RETURN(init_frame_states(thread->frame_states),
                  "Failed to init frame states", false);

  return true;
}

void destroy_render_thread(RenderThread *thread) {
  TB_CHECK(thread, "Invalid thread");

  VkAllocationCallbacks *vk_alloc = &thread->vk_alloc;
  Allocator std_alloc = thread->std_alloc.alloc;

  destroy_frame_states(thread->frame_states);

  vmaDestroyAllocator(thread->vma_alloc);

  vkDestroyDevice(thread->device, vk_alloc);

  destroy_gpu(std_alloc, thread->queue_props);

  // Destroy debug messenger
#ifdef VALIDATION
  vkDestroyDebugUtilsMessengerEXT(thread->instance,
                                  thread->debug_utils_messenger, vk_alloc);
#endif

  vkDestroyInstance(thread->instance, vk_alloc);

  *thread = (RenderThread){0};
}

int32_t render_thread(void *data) {
  RenderThread *thread = (RenderThread *)data;

  // Init
  TB_CHECK_RETURN(init_render_thread(thread), "Failed to init render thread",
                  -1);

  TracyCSetThreadName("Render Thread");

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

    { // TODO: Draw!
      // SDL_Log("Render Thread: idx (%d) count (%d)", thread->frame_idx,
      //        thread->frame_count);
    }

    // Increment frame count when done
    thread->frame_count++;
    thread->frame_idx = thread->frame_count % MAX_FRAME_STATES;

    // Signal frame done
    SDL_SemPost(frame_state->signal_sem);

    TracyCZoneEnd(ctx);
  }

  // Shutdown
  destroy_render_thread(thread);

  return 0;
}
