#pragma once

#include "dynarray.h"
#include "tbvk.h"
#include "tbvma.h"
#include "vkdbg.h"

#define TB_MAX_FRAME_STATES 3

#define TB_RP_LABEL_LEN 100

#define TB_VMA_TMP_GPU_MB 64
#define TB_MAX_ATTACHMENTS 4
#define TB_MAX_RENDER_PASS_DEPS 8
#define TB_MAX_RENDER_PASS_TRANS 16
#define TB_MAX_BARRIERS 16

typedef struct BufferCopy {
  VkBuffer src;
  VkBuffer dst;
  VkBufferCopy region;
} BufferCopy;

typedef struct BufferImageCopy {
  VkBuffer src;
  VkImage dst;
  VkBufferImageCopy region;
  VkImageSubresourceRange range;
} BufferImageCopy;

typedef TB_DYN_ARR_OF(VkWriteDescriptorSet) TbSetWriteQueue;
typedef TB_DYN_ARR_OF(BufferCopy) TbBufferCopyQueue;
typedef TB_DYN_ARR_OF(BufferImageCopy) TbBufferImageCopyQueue;

typedef struct TbHostBuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
  VmaAllocationInfo info;
  uint64_t offset;
} TbHostBuffer;

typedef struct TbBuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
  VmaAllocationInfo info;
} TbBuffer;

typedef struct TbImage {
  VkImage image;
  VkImageLayout layout;
  VmaAllocation alloc;
  VmaAllocationInfo info;
} TbImage;

typedef struct TbDrawBatch TbDrawBatch;
typedef struct TbDispatchBatch TbDispatchBatch;
typedef struct TracyCGPUContext TracyCGPUContext;

typedef void tb_record_draw_batch(TracyCGPUContext *gpu_ctx,
                                  VkCommandBuffer buffer, uint32_t batch_count,
                                  const TbDrawBatch *batches);
typedef void tb_record_dispatch_batch(TracyCGPUContext *gpu_ctx,
                                      VkCommandBuffer buffer,
                                      uint32_t batch_count,
                                      const TbDispatchBatch *batches);
