#pragma once

#include "allocator.h"

#include "tbvk.h"

#if !defined(FINAL) && !defined(__ANDROID__)
#define VALIDATION
#endif

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_semaphore SDL_semaphore;
typedef struct SDL_mutex SDL_mutex;

typedef struct mi_heap_s mi_heap_t;

typedef struct VmaAllocator_T *VmaAllocator;

typedef struct RenderThreadDescriptor {
  SDL_Window *window;
} RenderThreadDescriptor;

#define MAX_FRAME_STATES 3

typedef struct FrameState {
  SDL_semaphore *wait_sem;
  SDL_semaphore *signal_sem;

  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;
  void *tracy_gpu_context;

  VkImage swapchain_image;
  VkImageView swapchain_image_view;

  VkImageView depth_buffer_view;

  VkSemaphore img_acquired_sem;
  VkSemaphore swapchain_image_sem;
  VkSemaphore render_complete_sem;
  VkFence fence;
} FrameState;

typedef struct Swapchain {
  bool valid;
  VkSwapchainKHR swapchain;
  uint32_t image_count;
  VkFormat format;
  VkColorSpaceKHR color_space;
  VkPresentModeKHR present_mode;
  uint32_t width;
  uint32_t height;
} Swapchain;

typedef struct RenderExtensionSupport {
  bool portability : 1;
  bool raytracing : 1;
  bool calibrated_timestamps : 1;
} RenderExtensionSupport;

typedef struct RenderThread {
  SDL_Window *window;
  SDL_Thread *thread;

  StandardAllocator std_alloc;
  ArenaAllocator render_arena;

  mi_heap_t *vk_heap;
  VkAllocationCallbacks vk_alloc;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_utils_messenger;

  VkPhysicalDevice gpu;
  VkPhysicalDeviceProperties2 gpu_props;
  VkPhysicalDeviceDriverProperties driver_props;
  uint32_t queue_family_count;
  VkQueueFamilyProperties *queue_props;
  VkPhysicalDeviceFeatures gpu_features;
  VkPhysicalDeviceMemoryProperties gpu_mem_props;

  VkSurfaceKHR surface;
  uint32_t graphics_queue_family_index;
  uint32_t present_queue_family_index;

  RenderExtensionSupport ext_support;

  VkDevice device;
  VkQueue present_queue;
  VkQueue graphics_queue;

  VmaAllocator vma_alloc;

  Swapchain swapchain;

  VkPipelineCache pipeline_cache;

  VkSampler default_sampler;

  uint32_t frame_idx;
  uint64_t frame_count;
  FrameState frame_states[MAX_FRAME_STATES];
} RenderThread;

bool tb_start_render_thread(RenderThreadDescriptor *desc, RenderThread *thread);

void tb_signal_render(RenderThread *thread, uint32_t frame_idx);

void tb_wait_render(RenderThread *thread, uint32_t frame_idx);

void tb_stop_render_thread(RenderThread *thread);
