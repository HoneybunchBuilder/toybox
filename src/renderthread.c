#include "renderthread.h"

#include "mimalloc.h"
#include <stdbool.h>

#include "allocator.h"
#include "profiling.h"

#include "config.h"
#include "tbcommon.h"
#include "tbsdl.h"
#include "tbvk.h"

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
      SDL_assert(err == VK_SUCCESS);
      if (instance_layer_count > 0) {
        VkLayerProperties *instance_layers =
            tb_alloc_nm_tp(tmp_alloc, instance_layer_count, VkLayerProperties);
        err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                                                 instance_layers);
        SDL_assert(err == VK_SUCCESS);
#ifdef VALIDATION
        {
          const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

          bool validation_found = check_layer(
              validation_layer_name, instance_layer_count, instance_layers);
          if (validation_found) {
            SDL_assert(layer_count + 1 < MAX_LAYER_COUNT);
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

bool init_device(VkInstance instance, Allocator tmp_alloc,
                 VkAllocationCallbacks *vk_alloc, VkDevice *device) {
  (void)instance;
  (void)tmp_alloc;
  (void)vk_alloc;
  (void)device;
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

  TB_CHECK_RETURN(init_frame_states(thread->frame_states),
                  "Failed to init frame states", false);

  return true;
}

void destroy_render_thread(RenderThread *thread) {
  TB_CHECK(thread, "Invalid thread");

  destroy_frame_states(thread->frame_states);

  destroy_gpu(thread->std_alloc.alloc, thread->queue_props);

  // Destroy debug messenger
#ifdef VALIDATION
  vkDestroyDebugUtilsMessengerEXT(
      thread->instance, thread->debug_utils_messenger, &thread->vk_alloc);
#endif

  vkDestroyInstance(thread->instance, &thread->vk_alloc);

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
