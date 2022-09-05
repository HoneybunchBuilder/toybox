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

typedef struct TbBuffer {
  VkBuffer buffer;
  uint64_t offset;
  void *ptr;
} TbBuffer;

typedef struct TbImage {
  VkImage image;
  VmaAllocation alloc;
  VmaAllocationInfo info;
  void *ptr;
} TbImage;

typedef void tb_pass_record(VkCommandBuffer buffer, uint32_t batch_count,
                            const void *batches);
