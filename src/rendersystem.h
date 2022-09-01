#pragma once

#include "allocator.h"
#include "tbvk.h"
#include "tbvkalloc.h"

#define RenderSystemId 0xABADBABE

#define TB_VMA_TMP_HOST_BYTES 256

typedef struct SystemDescriptor SystemDescriptor;

typedef struct RenderThread RenderThread;

typedef struct mi_heap_s mi_heap_t;

typedef struct VmaPool_T *VmaPool;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct VkBuffer_T *VkBuffer;

typedef struct RenderSystemDescriptor {
  RenderThread *render_thread;
} RenderSystemDescriptor;

typedef struct RenderSystemFrameState {
  uint64_t tmp_host_size;
  VmaAllocation tmp_host_alloc;
  VkBuffer tmp_host_buffer;
  uint8_t *tmp_host_mapped;
  VmaPool tmp_host_pool;
} RenderSystemFrameState;

typedef struct RenderSystem {
  RenderThread *render_thread;

  VkHostAlloc vk_host_alloc;
  VkAllocationCallbacks vk_host_alloc_cb;

  VmaAllocator vma_alloc;

  uint32_t frame_idx;
  RenderSystemFrameState frame_states[3];
} RenderSystem;

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc);

bool tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                      VkBuffer *buffer, uint64_t *offset,
                                      void **ptr);
