#pragma once

#include "tbvk.h"
#include "tbvma.h"
#include "vkdbg.h"

#define TB_MAX_FRAME_STATES 3

typedef struct BufferCopy {
  VkBuffer src;
  VkBuffer dst;
  VkBufferCopy region;
} BufferCopy;

typedef struct BufferImageCopy {
  VkBuffer src;
  VkImage dst;
  VkBufferImageCopy region;
} BufferImageCopy;

typedef struct BufferCopyQueue {
  uint32_t req_count;
  BufferCopy *reqs;
  uint32_t req_max;
} BufferCopyQueue;

typedef struct BufferImageCopyQueue {
  uint32_t req_count;
  BufferImageCopy *reqs;
  uint32_t req_max;
} BufferImageCopyQueue;

typedef struct TbHostBuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
  VmaAllocationInfo info;
  uint64_t offset;
  void *ptr;
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

typedef void tb_record_draw_batch(VkCommandBuffer buffer, uint32_t batch_count,
                                  const void *batches);
