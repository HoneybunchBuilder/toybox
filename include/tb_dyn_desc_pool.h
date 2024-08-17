#pragma once

#include "tb_render_common.h"
#include "tb_render_system.h"

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
  uint32_t desc_count;
  uint32_t desc_cap;
  uint32_t binding;
  VkDescriptorSetLayout layout;
  VkDescriptorPool pools[TB_MAX_FRAME_STATES];
  VkDescriptorSet sets[TB_MAX_FRAME_STATES];
  bool resize[TB_MAX_FRAME_STATES];
  TB_DYN_ARR_OF(uint32_t) free_list;
  TB_DYN_ARR_OF(TbDynDescWrite) writes;
  TB_DYN_ARR_OF(uint32_t) write_queues[TB_MAX_FRAME_STATES];
} TbDynDescPool;

void tb_create_dyn_desc_pool(TbRenderSystem *rnd_sys,
                             VkDescriptorSetLayout layout, TbDynDescPool *pool,
                             uint32_t binding);

bool tb_write_dyn_desc_pool(TbDynDescPool *pool, uint32_t write_count,
                            const TbDynDescWrite *writes, uint32_t *out_idxs);

// Call this once per frame after you've issues any relevant writes
void tb_tick_dyn_desc_pool(TbRenderSystem *rnd_sys, TbDynDescPool *pool,
                           const char *name);

VkDescriptorSet tb_dyn_desc_pool_get_set(TbRenderSystem *rnd_sys,
                                         const TbDynDescPool *pool);
