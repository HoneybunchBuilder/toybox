#include "tb_descriptor_buffer.h"

#include "tb_common.h"
#include "tb_render_system.h"
#include "tb_util.h"

#define TB_DESC_BUF_PAGE_SIZE 64

VkResult tb_resize_desc_buffer(TbRenderSystem *rnd_sys, uint32_t capacity,
                               TbDescriptorBuffer *out_buf) {
  VkResult err = VK_SUCCESS;

  // The old buffer which we will copy from and schedule for clean-up later
  TbDescriptorBuffer prev_buf = *out_buf;

  const uint32_t cap = capacity > 0 ? capacity : 1;
  out_buf->desc_cap = cap;

  const VkDeviceSize buffer_size = capacity * out_buf->layout_size;
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    const char *buf_name = "";
#ifndef FINAL
    buf_name = out_buf->name;
#endif
    err = tb_rnd_sys_create_gpu_buffer(rnd_sys, &create_info, buf_name,
                                       &out_buf->buffer, &out_buf->host,
                                       (void **)&out_buf->data_ptr);
    TB_VK_CHECK(err, "Failed to create descriptor buffer");

    // Did the previous generation buffer have contents that we should copy?
    if (prev_buf.desc_count > 0) {
      const VkDeviceSize prev_buf_size =
          prev_buf.desc_cap * out_buf->layout_size;
      // NOTE: This may be extremely expensive. Driver owned memory usually
      // isn't supposed to be read from but maybe doing this all in one op will
      // be okay. Maybe a vkCmdCopyBuffer instead?
      SDL_memcpy(out_buf->data_ptr, prev_buf.data_ptr, prev_buf_size);
    }
  }

  // Free list can be initialized and all possible indices should be placed into
  // the list
  TB_DYN_ARR_RESET(out_buf->free_list, rnd_sys->gp_alloc, cap);
  // Reverse iter so the last idx we append is 0
  // Iter from the previous buf's cap so that we only reports newly allocated
  // indices as free. Existing free list is still correct
  for (uint32_t i = cap - 1; i > prev_buf.desc_cap; --i) {
    TB_DYN_ARR_APPEND(out_buf->free_list, i);
  }

  return err;
}

VkResult tb_create_descriptor_buffer(TbRenderSystem *rnd_sys,
                                     VkDescriptorSetLayout layout,
                                     const char *name, uint32_t capacity,
                                     TbDescriptorBuffer *out_buf) {
  const tb_auto alignment =
      rnd_sys->render_thread->desc_buf_props.descriptorBufferOffsetAlignment;

  VkDevice device = rnd_sys->render_thread->device;

  VkDeviceSize layout_size = 0;
  vkGetDescriptorSetLayoutSizeEXT(device, layout, &layout_size);
  layout_size = tb_calc_aligned_size(1, layout_size, alignment);

  *out_buf = (TbDescriptorBuffer){
      .layout = layout,
      .layout_size = layout_size,
  };

#ifndef FINAL
  const uint32_t name_len = SDL_strlen(name) + 1;
  char *buf_name = tb_alloc_nm_tp(rnd_sys->gp_alloc, name_len, char);
  SDL_strlcpy(buf_name, name, name_len);
  out_buf->name = buf_name;
#endif

  VkResult err = tb_resize_desc_buffer(rnd_sys, capacity, out_buf);
  TB_CHECK(err, "Error occurred during resize");
  return err;
}

uint32_t tb_write_desc_to_buffer(TbRenderSystem *rnd_sys,
                                 TbDescriptorBuffer *desc_buf, uint32_t binding,
                                 VkDescriptorType type,
                                 const VkDescriptorDataEXT *desc) {
  // See if we need to resize the buffer
  if (desc_buf->desc_count + 1 >= desc_buf->desc_cap) {
    const uint32_t new_cap = desc_buf->desc_cap + TB_DESC_BUF_PAGE_SIZE;
    VkResult err = tb_resize_desc_buffer(rnd_sys, new_cap, desc_buf);
    TB_CHECK(err, "Error occurred during resize");
  }

  // Take first free index
  TB_CHECK(TB_DYN_ARR_SIZE(desc_buf->free_list) > 0, "Free list exhausted");
  uint32_t idx = *TB_DYN_ARR_BACKPTR(desc_buf->free_list);
  TB_DYN_ARR_POP(desc_buf->free_list);

  VkDevice device = rnd_sys->render_thread->device;

  // Last minute look up binding offset
  VkDeviceSize binding_offset = 0;
  vkGetDescriptorSetLayoutBindingOffsetEXT(device, desc_buf->layout, binding,
                                           &binding_offset);
  VkDescriptorGetInfoEXT desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
      .type = type,
      .data = *desc,
  };
  tb_auto desc_buf_props = &rnd_sys->render_thread->desc_buf_props;
  VkDeviceSize desc_size = 0;
  switch (type) {
  case VK_DESCRIPTOR_TYPE_SAMPLER: {
    desc_size = desc_buf_props->samplerDescriptorSize;
    break;
  }
  case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
    desc_size = desc_buf_props->sampledImageDescriptorSize;
    break;
  }
  case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
    desc_size = desc_buf_props->storageImageDescriptorSize;
    break;
  }
  case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
    desc_size = desc_buf_props->uniformTexelBufferDescriptorSize;
    break;
  }
  case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
    desc_size = desc_buf_props->storageTexelBufferDescriptorSize;
    break;
  }
  case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
    desc_size = desc_buf_props->uniformBufferDescriptorSize;
    break;
  }
  case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
    desc_size = desc_buf_props->storageBufferDescriptorSize;
    break;
  }
  default: {
    desc_size = 0;
    break;
  }
  }
  TB_CHECK(desc_size, "Failed to determine descriptor size");

  // Write to the buffer by having vkGetDescriptorExt output to the buf's ptr
  VkDeviceSize offset = (idx * desc_size) + binding_offset;
  uint8_t *ptr = desc_buf->data_ptr + offset;
  vkGetDescriptorEXT(device, &desc_info, desc_size, ptr);

  // Upload just the region of the buffer that changed
  TbBufferCopy copy = {
      .dst = desc_buf->buffer.buffer,
      .src = desc_buf->host.buffer,
      .region = {.srcOffset = offset, .dstOffset = offset, .size = desc_size},
  };
  tb_rnd_upload_buffers(rnd_sys, &copy, 1);

  // Increase buffer count
  desc_buf->desc_count++;
  return idx;
}

void tb_free_desc_from_buffer(TbDescriptorBuffer *desc_buf, uint32_t idx) {
  TB_CHECK(desc_buf->desc_count > 0, "No descriptors exist to free");
  TB_CHECK(TB_DYN_ARR_SIZE(desc_buf->free_list) < desc_buf->desc_cap,
           "No space for free index");
  // We could be nice and set the data at ptr to 0 but that is unnecessary
  TB_DYN_ARR_APPEND(desc_buf->free_list, idx);
  --desc_buf->desc_count;
}
