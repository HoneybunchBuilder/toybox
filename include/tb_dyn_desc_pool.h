#pragma once

#include "tb_free_list.h"
#include "tb_render_common.h"
#include "tb_render_system.h"

#define TB_DESC_POOL_CAP 4096

typedef union TbDynDesc {
  VkDescriptorImageInfo image;
  VkDescriptorBufferInfo buffer;
  VkBufferView texel_buffer;
} TbDynDesc;

typedef struct TbDynDescWrite {
  VkDescriptorType type;
  TbDynDesc desc;
} TbDynDescWrite;

// A descriptor pool that contains a single set with a single type of resizable
// descriptor
typedef struct TbDynDescPool {

  uint32_t desc_cap;
  uint32_t binding;
  VkDescriptorSetLayout layout;
  VkDescriptorType type;
  VkDescriptorPool pools[TB_MAX_FRAME_STATES];
  VkDescriptorSet sets[TB_MAX_FRAME_STATES];
  TbFreeList free_list;
  TB_QUEUE_OF(VkWriteDescriptorSet) write_queues[TB_MAX_FRAME_STATES];
} TbDynDescPool;

void tb_create_dyn_desc_pool(TbRenderSystem *rnd_sys, const char *name,
                             VkDescriptorSetLayout layout,
                             VkDescriptorType type, uint32_t desc_cap,
                             TbDynDescPool *pool, uint32_t binding);

bool tb_write_dyn_desc_pool(TbDynDescPool *pool, uint32_t write_count,
                            const TbDynDescWrite *writes, uint32_t *out_idxs);

// Call this once per frame after you've issues any relevant writes
void tb_tick_dyn_desc_pool(TbRenderSystem *rnd_sys, TbDynDescPool *pool);

VkDescriptorSet tb_dyn_desc_pool_get_set(TbRenderSystem *rnd_sys,
                                         const TbDynDescPool *pool);
