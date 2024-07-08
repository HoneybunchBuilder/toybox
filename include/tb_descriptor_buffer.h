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

VkResult tb_create_descriptor_buffer(TbRenderSystem *rnd_sys,
                                     VkDescriptorSetLayout layout,
                                     const char *name, uint32_t capacity,
                                     TbDescriptorBuffer *out_buf);

void tb_destroy_descriptor_buffer(TbRenderSystem *rnd_sys,
                                  TbDescriptorBuffer *buf);

uint32_t tb_write_desc_to_buffer(TbRenderSystem *rnd_sys,
                                 TbDescriptorBuffer *desc_buf, uint32_t binding,
                                 VkDescriptorType type,
                                 const VkDescriptorDataEXT *desc);

void tb_free_desc_from_buffer(TbDescriptorBuffer *desc_buf, uint32_t idx);
