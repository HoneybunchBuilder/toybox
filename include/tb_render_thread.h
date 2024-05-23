#pragma once

#include "tb_allocator.h"
#include "tb_render_common.h"

#if !defined(FINAL) && !defined(__ANDROID__)
#define VALIDATION
#endif

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Semaphore SDL_Semaphore;
typedef struct SDL_mutex SDL_mutex;

typedef struct mi_heap_s mi_heap_t;

typedef struct VmaAllocation_T *VmaAllocation;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaPool_T *VmaPool;

typedef uint32_t TbRenderPassId;

typedef struct TbRenderThreadDescriptor {
  SDL_Window *window;
} TbRenderThreadDescriptor;

typedef struct TbImageTransition {
  VkPipelineStageFlags src_flags;
  VkPipelineStageFlags dst_flags;
  VkImageMemoryBarrier barrier;
} TbImageTransition;

typedef struct VkRenderingInfo VkRenderingInfo;

typedef struct TbPassContext {
  TbRenderPassId id;
  uint32_t command_buffer_index;
  uint32_t attachment_count;
  VkClearValue clear_values[TB_MAX_ATTACHMENTS];
  uint32_t width;
  uint32_t height;

  uint32_t barrier_count;
  TbImageTransition barriers[TB_MAX_BARRIERS];

  VkRenderingInfo *render_info;

#ifdef TRACY_ENABLE
  char label[TB_RP_LABEL_LEN];
#endif
} TbPassContext;

typedef struct TbDrawBatch TbDrawBatch;

typedef struct TbDrawContext {
  TbRenderPassId pass_id;
  tb_record_draw_batch_fn *record_fn;
  uint32_t batch_count;
  TbDrawBatch *batches;
  uint32_t user_batch_size;
  void *user_batches;
  uint32_t batch_max;
} TbDrawContext;

typedef struct TbDispatchBatch TbDispatchBatch;

typedef struct TbDispatchContext {
  TbRenderPassId pass_id;
  tb_record_dispatch_batch_fn *record_fn;
  uint32_t batch_count;
  TbDispatchBatch *batches;
  uint32_t user_batch_size;
  void *user_batches;
  uint32_t batch_max;
} TbDispatchContext;

#define TB_MAX_COMMAND_BUFFERS 64

typedef struct TbFrameState {
  SDL_Semaphore *wait_sem;
  SDL_Semaphore *signal_sem;

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

  // References to queues owned by the main thread and pushed to by tasks
  // The render thread only consumes these
  TbSetWriteQueue *set_write_queue;
  TbBufferCopyQueue *buf_copy_queue;
  TbBufferImageCopyQueue *buf_img_copy_queue;

  TbArenaAllocator tmp_alloc;

  TB_DYN_ARR_OF(TbPassContext) pass_contexts;
  TB_DYN_ARR_OF(TbDrawContext) draw_contexts;
  TB_DYN_ARR_OF(TbDispatchContext) dispatch_contexts;
} TbFrameState;

typedef struct TbSwapchain {
  bool valid;
  VkSwapchainKHR swapchain;
  uint32_t image_count;
  VkFormat format;
  VkColorSpaceKHR color_space;
  VkPresentModeKHR present_mode;
  uint32_t width;
  uint32_t height;
} TbSwapchain;

typedef struct TbRenderExtensionSupport {
  bool portability : 1;
  bool calibrated_timestamps : 1;
} TbRenderExtensionSupport;

typedef struct TbRenderThread {
  SDL_Window *window;
  SDL_Thread *thread;
  SDL_Semaphore *initialized;
  SDL_Semaphore *resized;

  TbGeneralAllocator gp_alloc;
  TbArenaAllocator render_arena;

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

  TbRenderExtensionSupport ext_support;

  VkDevice device;
  VkQueue present_queue;
  VkQueue graphics_queue;

  VmaAllocator vma_alloc;

  TbSwapchain swapchain;

  VkSampler default_sampler;

  uint32_t frame_idx;
  uint64_t frame_count;
  TbFrameState frame_states[TB_MAX_FRAME_STATES];

  uint8_t stop_signal;
  uint8_t swapchain_resize_signal;
} TbRenderThread;

bool tb_start_render_thread(TbRenderThreadDescriptor *desc,
                            TbRenderThread *thread);

void tb_signal_render(TbRenderThread *thread, uint32_t frame_idx);

void tb_wait_render(TbRenderThread *thread, uint32_t frame_idx);

void tb_wait_thread_initialized(TbRenderThread *thread);

void tb_stop_render_thread(TbRenderThread *thread);

void tb_destroy_render_thread(TbRenderThread *thread);
