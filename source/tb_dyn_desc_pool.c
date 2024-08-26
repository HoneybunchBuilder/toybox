#include "tb_dyn_desc_pool.h"
#include "tb_common.h"
#include "tb_util.h"

void tb_create_dyn_desc_pool(TbRenderSystem *rnd_sys, const char *name,
                             VkDescriptorSetLayout layout,
                             VkDescriptorType type, uint32_t desc_cap,
                             TbDynDescPool *pool, uint32_t binding) {
  TB_TRACY_SCOPE("Create Dynamic Descriptor Pool");
  *pool = (TbDynDescPool){
      .layout = layout,
      .binding = binding,
      .type = type,
      .desc_cap = desc_cap,
  };
  tb_reset_free_list(rnd_sys->gp_alloc, &pool->free_list, pool->desc_cap);
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .type = pool->type,
                    .descriptorCount = pool->desc_cap,
                },
            },
    };
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

    TB_QUEUE_RESET(pool->write_queues[frame_idx], rnd_sys->gp_alloc, desc_cap);
  }
}

bool tb_write_dyn_desc_pool(TbDynDescPool *pool, uint32_t write_count,
                            const TbDynDescWrite *writes, uint32_t *out_idxs) {
  TB_TRACY_SCOPE("Write Dynamic Descriptor Pool");
  if (write_count == 0) {
    return true;
  }
  TB_CHECK_RETURN(TB_DYN_ARR_SIZE(pool->free_list) >= write_count,
                  "Not enough space for writes", false);

  for (uint32_t i = 0; i < write_count; ++i) {
    uint32_t free_idx = 0;
    bool idx_ok = tb_pull_index(&pool->free_list, &free_idx);
    TB_CHECK(idx_ok, "Failed to retrieve index from free list");

    tb_auto desc_write = &writes[i];
    TB_CHECK(pool->type == desc_write->type, "Invalid write type");

    // Need to enqueue a write per frame
    for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
      tb_auto write_queue = &pool->write_queues[frame_idx];

      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = pool->type,
          .dstBinding = pool->binding,
          .dstSet = pool->sets[frame_idx],
          .dstArrayElement = free_idx,
      };

      if (pool->type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
          pool->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
          pool->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        write.pImageInfo = &desc_write->desc.image;
        if (write.pImageInfo->imageView == VK_NULL_HANDLE) {
          continue;
        }
      } else if (pool->type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                 pool->type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
        write.pTexelBufferView = &desc_write->desc.texel_buffer;
        if (write.pTexelBufferView == VK_NULL_HANDLE) {
          continue;
        }
      } else if (pool->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                 pool->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        write.pBufferInfo = &desc_write->desc.buffer;
        if (write.pBufferInfo->buffer == VK_NULL_HANDLE) {
          continue;
        }
      } else {
        TB_CHECK(false, "Unexpected descriptor type");
      }

      TB_QUEUE_PUSH_PTR(write_queue, write);
    }
    out_idxs[i] = free_idx;
  }
  return true;
}

void tb_tick_dyn_desc_pool(TbRenderSystem *rnd_sys, TbDynDescPool *pool) {
  TB_TRACY_SCOPE("Tick Dynamic Descriptor Pool");
  const uint32_t frame_idx = rnd_sys->frame_idx;
  tb_auto write_queue = &pool->write_queues[frame_idx];

  // Render Thread allocator to make sure writes live
  tb_auto rnd_tmp_alloc =
      rnd_sys->render_thread->frame_states[rnd_sys->frame_idx].tmp_alloc.alloc;

  // Dequeue to a local collection
  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  TB_DYN_ARR_RESET(writes, rnd_tmp_alloc, pool->desc_cap);
  VkWriteDescriptorSet write = {0};
  while (TB_QUEUE_POP(*write_queue, &write)) {
    TB_DYN_ARR_APPEND(writes, write);
  }

  // Issue any writes that were gathered
  if (!TB_DYN_ARR_EMPTY(writes)) {
    tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);
  }
}

VkDescriptorSet tb_dyn_desc_pool_get_set(TbRenderSystem *rnd_sys,
                                         const TbDynDescPool *pool) {
  return pool->sets[rnd_sys->frame_idx];
}
