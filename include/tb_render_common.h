#pragma once

#include "tb_dynarray.h"
#include "tb_queue.h"
#include "tb_vk.h"
#include "tb_vk_dbg.h"
#include "tb_vma.h"

#define TB_MAX_FRAME_STATES 3

#define TB_RP_LABEL_LEN 100

#define TB_VMA_TMP_GPU_MB 64
#define TB_MAX_ATTACHMENTS 4
#define TB_MAX_RENDER_PASS_DEPS 8
#define TB_MAX_RENDER_PASS_TRANS 16
#define TB_MAX_BARRIERS 16

// TEMP: For migrating to descriptor buffers
#define TB_USE_DESC_BUFFER 0

typedef struct TbDrawBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;
  VkViewport viewport;
  VkRect2D scissor;
  void *user_batch;
  uint32_t draw_count;
  uint64_t draw_size;
  void *draws;
  uint32_t draw_max;
} TbDrawBatch;

#define MAX_GROUPS 8
typedef struct TbDispatchBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;
  void *user_batch;
  uint32_t group_count;
  uint3 groups[MAX_GROUPS];
} TbDispatchBatch;

typedef struct TbFullscreenBatch {
  VkDescriptorSet set;
} TbFullscreenBatch;

typedef struct TbFrameDescriptorPool {
  uint32_t set_count;
  uint32_t desc_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
  TB_DYN_ARR_OF(uint32_t) free_list;
} TbFrameDescriptorPool;

typedef struct TbDescriptorPool {
  uint64_t count;
  VkDescriptorPool pool;
  uint64_t capacity;
  VkDescriptorSet *sets;
} TbDescriptorPool;

typedef struct TbFrameDescriptorPoolList {
  TbFrameDescriptorPool pools[TB_MAX_FRAME_STATES];
} TbFrameDescriptorPoolList;

typedef struct TbResourceId {
  uint64_t id;
  uint32_t idx;
} TbResourceId;

typedef struct TbBufferCopy {
  VkBuffer src;
  VkBuffer dst;
  VkBufferCopy region;
} TbBufferCopy;

typedef struct TbBufferImageCopy {
  VkBuffer src;
  VkImage dst;
  VkBufferImageCopy region;
  VkImageSubresourceRange range;
} TbBufferImageCopy;

typedef TB_QUEUE_OF(VkWriteDescriptorSet) TbSetWriteQueue;
typedef TB_QUEUE_OF(TbBufferCopy) TbBufferCopyQueue;
typedef TB_QUEUE_OF(TbBufferImageCopy) TbBufferImageCopyQueue;

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
  VkDeviceAddress address;
} TbBuffer;

typedef struct TbImage {
  VkImage image;
  VkImageLayout layout;
  VmaAllocation alloc;
  VmaAllocationInfo info;
} TbImage;

typedef struct TracyCGPUContext TracyCGPUContext;

typedef void tb_record_draw_batch_fn(TracyCGPUContext *gpu_ctx,
                                     VkCommandBuffer buffer,
                                     uint32_t batch_count,
                                     const TbDrawBatch *batches);
typedef void tb_record_dispatch_batch_fn(TracyCGPUContext *gpu_ctx,
                                         VkCommandBuffer buffer,
                                         uint32_t batch_count,
                                         const TbDispatchBatch *batches);

void tb_record_fullscreen(VkCommandBuffer buffer, const TbDrawBatch *batch,
                          const TbFullscreenBatch *fs_batch);
