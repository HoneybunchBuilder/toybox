#pragma once

#include "tb_render_common.h"
#include "tb_vk.h"

typedef struct TbRenderSystem TbRenderSystem;

typedef struct TbDescriptorBuffer {
  VkDescriptorSetLayout layout;
  VkDeviceSize layout_size;
  uint32_t desc_count;
  uint32_t desc_cap;
  TB_DYN_ARR_OF(uint32_t) free_list;
  TbBuffer buffer;
  TbHostBuffer host;
  uint8_t *data_ptr;
#ifndef FINAL
  const char *name;
#endif
} TbDescriptorBuffer;

typedef struct TbDescriptor {
  VkDescriptorType type;
  VkDescriptorDataEXT data;
} TbDescriptor;

VkResult tb_create_descriptor_buffer(TbRenderSystem *rnd_sys,
                                     VkDescriptorSetLayout layout,
                                     const char *name, uint32_t capacity,
                                     TbDescriptorBuffer *out_buf);

void tb_destroy_descriptor_buffer(TbRenderSystem *rnd_sys,
                                  TbDescriptorBuffer *buf);

// Returns the index of the descriptor in the buffer
uint32_t tb_write_desc_to_buffer(TbRenderSystem *rnd_sys,
                                 TbDescriptorBuffer *desc_buf, uint32_t binding,
                                 const TbDescriptor *desc);

// For freeing an individual descriptor from the buffer
void tb_free_desc_from_buffer(TbDescriptorBuffer *desc_buf, uint32_t idx);

// Resets the internal free-list and invalidates all previously written
// descriptors
void tb_reset_descriptor_buffer(TbRenderSystem *rnd_sys,
                                TbDescriptorBuffer *desc_buf);
