#pragma once

#include "allocator.h"

#include "tbrendercommon.h"

#if !defined(FINAL) && !defined(__ANDROID__)
#define VALIDATION
#endif

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_semaphore SDL_semaphore;
typedef struct SDL_mutex SDL_mutex;

typedef struct mi_heap_s mi_heap_t;

typedef struct VmaAllocation_T *VmaAllocation;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaPool_T *VmaPool;

typedef struct RenderThreadDescriptor {
  SDL_Window *window;
} RenderThreadDescriptor;

#define TB_VMA_TMP_GPU_MB 256

typedef struct PassDrawCtx {
  VkRenderPass pass;
  VkFramebuffer framebuffer;
  uint32_t width;
  uint32_t height;
  tb_pass_record *record_cb;
  uint32_t batch_count;
  uint32_t batch_size;
  const void *batches;
} PassDrawCtx;

typedef struct FrameState {
  SDL_semaphore *wait_sem;
  SDL_semaphore *signal_sem;

  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;
  void *tracy_gpu_context;

  VkImage swapchain_image;
  VkImageView swapchain_image_view;

  TbImage depth_buffer;
  VkImageView depth_buffer_view;

  TbImage depth_buffer_copy;
  VkImageView depth_copy_view;

  VkSemaphore img_acquired_sem;
  VkSemaphore swapchain_image_sem;
  VkSemaphore render_complete_sem;
  VkFence fence;

  VmaAllocation tmp_gpu_alloc;
  VkBuffer tmp_gpu_buffer;
  VmaPool tmp_gpu_pool;

  // Memory expected to be actually allocated by the main thread
  // The main thread will write to this and the render thread will read it
  BufferCopyQueue buf_copy_queue;
  BufferImageCopyQueue buf_img_copy_queue;

  ArenaAllocator tmp_alloc;

  uint32_t pass_count;
  PassDrawCtx *pass_draw_contexts;
  uint32_t pass_max;

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
  SDL_semaphore *initialized;

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
  FrameState frame_states[TB_MAX_FRAME_STATES];

  uint32_t stop_signal;
} RenderThread;

bool tb_start_render_thread(RenderThreadDescriptor *desc, RenderThread *thread);

void tb_signal_render(RenderThread *thread, uint32_t frame_idx);

void tb_wait_render(RenderThread *thread, uint32_t frame_idx);

void tb_wait_thread_initialized(RenderThread *thread);

void tb_stop_render_thread(RenderThread *thread);

void tb_destroy_render_thread(RenderThread *thread);
