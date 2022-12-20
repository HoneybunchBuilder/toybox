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

typedef uint32_t TbRenderPassId;

typedef struct RenderThreadDescriptor {
  SDL_Window *window;
} RenderThreadDescriptor;

typedef struct ImageTransition {
  VkPipelineStageFlags src_flags;
  VkPipelineStageFlags dst_flags;
  VkImageMemoryBarrier barrier;
} ImageTransition;

typedef struct PassContext {
  TbRenderPassId id;
  uint32_t command_buffer_index;
  VkRenderPass pass;
  uint32_t attachment_count;
  VkClearValue clear_values[TB_MAX_ATTACHMENTS];
  VkFramebuffer framebuffer;
  uint32_t width;
  uint32_t height;

  uint32_t barrier_count;
  ImageTransition barriers[TB_MAX_BARRIERS];

#ifdef TRACY_ENABLE
  char label[TB_RP_LABEL_LEN];
#endif
} PassContext;

typedef struct DrawContext {
  TbRenderPassId pass_id;
  tb_record_draw_batch *record_fn;
  uint32_t batch_count;
  uint32_t batch_size;
  void *batches;
  uint32_t batch_max;
} DrawContext;

#define TB_MAX_COMMAND_BUFFERS 32

typedef struct FrameState {
  SDL_semaphore *wait_sem;
  SDL_semaphore *signal_sem;

  VkCommandPool command_pool;
  VkCommandBuffer base_command_buffers[2];
  uint32_t pass_command_buffer_count;
  VkCommandBuffer pass_command_buffers[TB_MAX_COMMAND_BUFFERS];
  void *tracy_gpu_context;

  VkImage swapchain_image;

  VkSemaphore img_acquired_sem;
  VkSemaphore swapchain_image_sem;
  VkSemaphore upload_complete_sem;
  VkSemaphore render_complete_sem;
  VkSemaphore frame_complete_sem;
  VkFence fence;

  VmaAllocation tmp_gpu_alloc;
  VkBuffer tmp_gpu_buffer;
  VmaPool tmp_gpu_pool;

  // Memory expected to be actually allocated by the main thread
  // The main thread will write to this and the render thread will read it
  BufferCopyQueue buf_copy_queue;
  BufferImageCopyQueue buf_img_copy_queue;

  ArenaAllocator tmp_alloc;

  uint32_t pass_ctx_count;
  PassContext *pass_contexts;
  uint32_t pass_ctx_max;

  uint32_t draw_ctx_count;
  DrawContext *draw_contexts;
  uint32_t draw_ctx_max;

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
  SDL_semaphore *resized;

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

  uint8_t stop_signal;
  uint8_t swapchain_resize_signal;
} RenderThread;

bool tb_start_render_thread(RenderThreadDescriptor *desc, RenderThread *thread);

void tb_signal_render(RenderThread *thread, uint32_t frame_idx);

void tb_wait_render(RenderThread *thread, uint32_t frame_idx);

void tb_wait_thread_initialized(RenderThread *thread);

void tb_stop_render_thread(RenderThread *thread);

void tb_destroy_render_thread(RenderThread *thread);
