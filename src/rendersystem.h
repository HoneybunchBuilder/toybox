#pragma once

#include "allocator.h"
#include "renderthread.h"
#include "tbvkalloc.h"

#define RenderSystemId 0xABADBABE

#define TB_VMA_TMP_HOST_MB 256

typedef struct SystemDescriptor SystemDescriptor;

typedef struct mi_heap_s mi_heap_t;

typedef struct VmaPool_T *VmaPool;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct VkBuffer_T *VkBuffer;

typedef struct RenderSystemDescriptor {
  Allocator std_alloc;
  RenderThread *render_thread;
} RenderSystemDescriptor;

typedef struct RenderSystemFrameState {
  uint64_t tmp_host_size;
  VmaAllocation tmp_host_alloc;
  VkBuffer tmp_host_buffer;
  uint8_t *tmp_host_mapped;
  VmaPool tmp_host_pool;

  BufferUploadQueue buffer_up_queue;
} RenderSystemFrameState;

typedef struct RenderSystem {
  Allocator std_alloc;
  RenderThread *render_thread;

  VkAllocationCallbacks vk_host_alloc_cb;

  VmaAllocator vma_alloc;

  uint32_t frame_idx;
  RenderSystemFrameState frame_states[3];
} RenderSystem;

typedef struct TbBuffer {
  VkBuffer buffer;
  uint64_t offset;
  void *ptr;
} TbBuffer;

typedef struct TbImage {
  VkImage image;
  void *ptr;
} TbImage;

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc);

VkResult tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                          TbBuffer *buffer);

VkResult tb_rnd_sys_alloc_host_image(RenderSystem *self,
                                     const VkImageCreateInfo *create_info,
                                     TbImage *image);

void tb_rnd_upload_buffers(RenderSystem *self, BufferUpload *uploads,
                           uint32_t upload_count);
