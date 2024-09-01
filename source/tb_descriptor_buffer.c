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
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
  for (int32_t i = (int32_t)cap - 1; i >= (int32_t)prev_buf.desc_cap; --i) {
    TB_DYN_ARR_APPEND(out_buf->free_list, i);
  }

  return err;
}

VkResult tb_create_descriptor_buffer(TbRenderSystem *rnd_sys,
                                     VkDescriptorSetLayout layout,
                                     const char *name, uint32_t capacity,
                                     TbDescriptorBuffer *out_buf) {
#if FINAL
  (void)name;
#endif
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
  TB_VK_CHECK(err, "Error occurred during resize");
  return err;
}

void tb_destroy_descriptor_buffer(TbRenderSystem *rnd_sys,
                                  TbDescriptorBuffer *buf) {
  (void)rnd_sys;
  (void)buf;
  TB_CHECK(false, "Unimplemented");
}

VkDescriptorBufferBindingInfoEXT
tb_desc_buff_get_binding(const TbDescriptorBuffer *desc_buf) {
  return (VkDescriptorBufferBindingInfoEXT){
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
      .address = desc_buf->buffer.address,
      .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
}

VkDeviceSize tb_lookup_desc_size(
    VkDescriptorType type,
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *props) {
  switch (type) {
  case VK_DESCRIPTOR_TYPE_SAMPLER: {
    return props->samplerDescriptorSize;
  }
  case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
    return props->sampledImageDescriptorSize;
  }
  case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
    return props->storageImageDescriptorSize;
  }
  case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
    return props->uniformTexelBufferDescriptorSize;
  }
  case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
    return props->storageTexelBufferDescriptorSize;
  }
  case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
    return props->uniformBufferDescriptorSize;
  }
  case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
    return props->storageBufferDescriptorSize;
  }
  default: {
    TB_CHECK(false, "Failed to determine descriptor size");
    return 0;
  }
  }
}

uint32_t tb_write_desc_to_buffer(TbRenderSystem *rnd_sys,
                                 TbDescriptorBuffer *desc_buf, uint32_t binding,
                                 const TbDescriptor *desc) {
  // See if we need to resize the buffer
  if (desc_buf->desc_count + 1 >= desc_buf->desc_cap) {
    const uint32_t new_cap = desc_buf->desc_cap + TB_DESC_BUF_PAGE_SIZE;
    VkResult err = tb_resize_desc_buffer(rnd_sys, new_cap, desc_buf);
    TB_VK_CHECK(err, "Error occurred during resize");
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
      .type = desc->type,
      .data = desc->data,
  };
  tb_auto desc_buf_props = &rnd_sys->render_thread->desc_buf_props;
  tb_auto desc_size = tb_lookup_desc_size(desc->type, desc_buf_props);

  // Write to the buffer by having vkGetDescriptorExt output to the buf's ptr
  VkDeviceSize offset = (idx * desc_size) + binding_offset;
  uint8_t *ptr = desc_buf->data_ptr + offset;
  vkGetDescriptorEXT(device, &desc_info, desc_size, ptr);

  // Host will be null if the buffer was mappable
  if (desc_buf->host.buffer != VK_NULL_HANDLE) {
    // NOTE: This could cause a lot of time spent processing buffer uploads
    // Upload just the region of the buffer that changed
    TbBufferCopy copy = {
        .dst = desc_buf->buffer.buffer,
        .src = desc_buf->host.buffer,
        .region = {.srcOffset = offset, .dstOffset = offset, .size = desc_size},
    };
    tb_rnd_upload_buffers(rnd_sys, &copy, 1);
  }

  // Increase buffer count
  desc_buf->desc_count++;
  return idx;
}

void tb_free_desc_from_buffer(TbDescriptorBuffer *desc_buf, uint32_t idx) {
  TB_CHECK(desc_buf->desc_count > 0, "No descriptors exist to free");
  TB_CHECK(TB_DYN_ARR_SIZE(desc_buf->free_list) < desc_buf->desc_cap,
           "No space for free index");
  // If the idx is already free, do nothing
  TB_DYN_ARR_FOREACH(desc_buf->free_list, i) {
    tb_auto free_idx = TB_DYN_ARR_AT(desc_buf->free_list, i);
    if (free_idx == idx) {
      return;
    }
  }
  // We could be nice and set the data at ptr to 0 but that is unnecessary
  TB_DYN_ARR_APPEND(desc_buf->free_list, idx);
  --desc_buf->desc_count;
}

// Resets the internal free-list and invalidates all previously written
// descriptors
void tb_reset_descriptor_buffer(TbRenderSystem *rnd_sys,
                                TbDescriptorBuffer *desc_buf) {
  tb_auto cap = desc_buf->desc_cap;
  // Free list can be initialized and all possible indices should be placed into
  // the list
  TB_DYN_ARR_RESET(desc_buf->free_list, rnd_sys->gp_alloc, cap);
  // Reverse iter so the last idx we append is 0
  // Iter from the previous buf's cap so that we only reports newly allocated
  // indices as free. Existing free list is still correct
  for (int32_t i = (int32_t)cap - 1; i >= 0; --i) {
    TB_DYN_ARR_APPEND(desc_buf->free_list, i);
  }
  desc_buf->desc_count = 0;
}
