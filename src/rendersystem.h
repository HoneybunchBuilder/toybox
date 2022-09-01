#pragma once

#include "allocator.h"
#include "tbvk.h"
#include "tbvkalloc.h"

#define RenderSystemId 0xABADBABE

#define MAX_VMA_TMP_HOST_BLOCK_COUNT 256

typedef struct SystemDescriptor SystemDescriptor;

typedef struct RenderThread RenderThread;

typedef struct mi_heap_s mi_heap_t;

typedef struct VmaPool_T *VmaPool;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct RenderSystemDescriptor {
  RenderThread *render_thread;
} RenderSystemDescriptor;

typedef struct RenderSystemFrameState {
  uint32_t tmp_host_blocks_allocated;
  VmaAllocation tmp_host_allocs[MAX_VMA_TMP_HOST_BLOCK_COUNT];
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
                                      uint32_t memory_usage,
                                      uint32_t buffer_usage, VkBuffer *buffer);
