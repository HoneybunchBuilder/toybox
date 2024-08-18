#include "tb_dyn_desc_pool.h"
#include "tb_common.h"
#include "tb_util.h"

static const uint32_t TbDynDescPageSize = 64;

void tb_resize_dyn_desc_pool(TbRenderSystem *rnd_sys, const char *name,
                             TbDynDescPool *pool, uint32_t frame_idx) {
  VkDescriptorPoolCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes =
          (VkDescriptorPoolSize[1]){
              {
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  .descriptorCount = 1,
              },
          },
  };

  if (pool->pools[frame_idx] != VK_NULL_HANDLE) {
    tb_rnd_destroy_descriptor_pool(rnd_sys, pool->pools[frame_idx]);
  }
  tb_rnd_create_descriptor_pool(rnd_sys, &create_info, name,
                                &pool->pools[frame_idx]);

  VkDescriptorSetVariableDescriptorCountAllocateInfo count_info = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
      .descriptorSetCount = 1,
      .pDescriptorCounts = (uint32_t[1]){pool->desc_cap},
  };
  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = &count_info,
      .descriptorSetCount = 1,
      .descriptorPool = pool->pools[frame_idx],
      .pSetLayouts = &pool->layout,
  };
  tb_rnd_alloc_descriptor_sets(rnd_sys, name, &alloc_info,
                               &pool->sets[frame_idx]);
}

void tb_create_dyn_desc_pool(TbRenderSystem *rnd_sys,
                             VkDescriptorSetLayout layout, TbDynDescPool *pool,
                             uint32_t binding) {
  *pool = (TbDynDescPool){
      .layout = layout,
      .binding = binding,
  };
  TB_DYN_ARR_RESET(pool->free_list, rnd_sys->gp_alloc, 16);
  TB_DYN_ARR_RESET(pool->writes, rnd_sys->gp_alloc, 16);
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    TB_DYN_ARR_RESET(pool->write_queues[i], rnd_sys->gp_alloc, 16);
  }
}

bool tb_write_dyn_desc_pool(TbDynDescPool *pool, uint32_t write_count,
                            const TbDynDescWrite *writes, uint32_t *out_idxs) {
  if (write_count == 0) {
    return true;
  }

  const uint64_t next_desc_count = pool->desc_count + write_count;
  if (next_desc_count >= pool->desc_cap) {
    const uint64_t old_desc_cap = pool->desc_cap;
    pool->desc_cap =
        tb_calc_aligned_size(next_desc_count, 1, TbDynDescPageSize);

    // Resize containers
    TB_DYN_ARR_RESERVE(pool->free_list, pool->desc_cap);
    TB_DYN_ARR_RESIZE(pool->writes, pool->desc_cap);

    // Add newly created indices to the free list
    for (int32_t i = (pool->desc_cap - 1); i >= (int32_t)old_desc_cap; --i) {
      TB_DYN_ARR_APPEND(pool->free_list, i);
      // New writes are ignored
      TB_DYN_ARR_AT(pool->writes, i).type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    // If we had to resize, mark every frame as needing resize
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      pool->resize[i] = true;
    }
  }

  TB_CHECK_RETURN(TB_DYN_ARR_SIZE(pool->free_list) >= write_count,
                  "Not enough space for writes", false);
  for (uint32_t i = 0; i < write_count; ++i) {
    uint32_t free_idx = *TB_DYN_ARR_BACKPTR(pool->free_list);
    TB_DYN_ARR_POP(pool->free_list);
    TB_DYN_ARR_AT(pool->writes, free_idx) = writes[i];
    // Mark that every frame pool needs the resource at this index written
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      TB_DYN_ARR_APPEND(pool->write_queues[i], free_idx);
    }
    out_idxs[i] = free_idx;
  }

  pool->desc_count = next_desc_count;
  return true;
}

void tb_tick_dyn_desc_pool(TbRenderSystem *rnd_sys, TbDynDescPool *pool,
                           const char *name) {
  const uint32_t frame_idx = rnd_sys->frame_idx;
  if (pool->desc_cap == 0 || pool->desc_count == 0) {
    return;
  }
  tb_auto write_queue = &pool->write_queues[frame_idx];

  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  TB_DYN_ARR_RESET(writes, rnd_sys->tmp_alloc, pool->desc_cap);

  // Resize the pool if needed
  if (pool->resize[frame_idx]) {
    pool->resize[frame_idx] = false;
    tb_resize_dyn_desc_pool(rnd_sys, name, pool, frame_idx);

    // Rewrite all descriptors
    for (uint32_t i = 0; i < pool->desc_count; ++i) {
      tb_auto desc_write = &TB_DYN_ARR_AT(pool->writes, i);
      tb_auto type = desc_write->type;
      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = desc_write->type,
          .dstBinding = pool->binding,
          .dstSet = pool->sets[frame_idx],
          .dstArrayElement = i,
      };
      // Skip unused descriptors
      if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
        continue;
      } else if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                 type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                 type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        write.pImageInfo = &desc_write->desc.image;
      } else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                 type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
        write.pTexelBufferView = &desc_write->desc.texel_buffer;
      } else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                 type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        write.pBufferInfo = &desc_write->desc.buffer;
      } else {
        TB_CHECK(false, "Unexpected descriptor type");
      }
      TB_DYN_ARR_APPEND(writes, write);
    }

  } else if (!TB_DYN_ARR_EMPTY(*write_queue)) {
    // If we didn't have to resize the pool we should just aim to issue
    // queued writes
    TB_DYN_ARR_FOREACH(*write_queue, i) {
      tb_auto write_idx = TB_DYN_ARR_AT(*write_queue, i);
      tb_auto desc_write = &TB_DYN_ARR_AT(pool->writes, write_idx);
      tb_auto type = desc_write->type;
      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = desc_write->type,
          .dstBinding = pool->binding,
          .dstSet = pool->sets[frame_idx],
          .dstArrayElement = write_idx,
      };
      // Skip unused descriptors
      if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
        continue;
      } else if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                 type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                 type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        write.pImageInfo = &desc_write->desc.image;
      } else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                 type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
        write.pTexelBufferView = &desc_write->desc.texel_buffer;
      } else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                 type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        write.pBufferInfo = &desc_write->desc.buffer;
      } else {
        TB_CHECK(false, "Unexpected descriptor type");
      }
      TB_DYN_ARR_APPEND(writes, write);
    }
  }
  // Clear write queue
  TB_DYN_ARR_RESIZE(pool->write_queues[frame_idx], 0);
  // Issue any writes that were gathered
  if (!TB_DYN_ARR_EMPTY(writes)) {
    tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);
  }
}

VkDescriptorSet tb_dyn_desc_pool_get_set(TbRenderSystem *rnd_sys,
                                         const TbDynDescPool *pool) {
  return pool->sets[rnd_sys->frame_idx];
}
