#pragma once

#include "allocator.h"
#include "renderthread.h"
#include "tbvkalloc.h"
#include "tbvma.h"

#define TB_VMA_TMP_HOST_MB 256
#define TB_MAX_LAYERS 16
#define TB_MAX_MIPS 16

typedef struct TbWorld TbWorld;

typedef struct TbRenderSystemFrameState {
  TbHostBuffer tmp_host_buffer;
  TbSetWriteQueue set_write_queue;
  TbBufferCopyQueue buf_copy_queue;
  TbBufferImageCopyQueue buf_img_copy_queue;
} TbRenderSystemFrameState;

typedef struct TbFrameDescriptorPool {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
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

typedef struct TbRenderSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;
  TbRenderThread *render_thread;

  VkAllocationCallbacks vk_host_alloc_cb;
  VmaAllocator vma_alloc;

  VkPipelineCache pipeline_cache;

  uint32_t frame_idx;
  TbRenderSystemFrameState frame_states[3];
} TbRenderSystem;

void tb_register_render_sys(TbWorld *world, TbRenderThread *render_thread);
void tb_unregister_render_sys(TbWorld *world);

VkResult tb_rnd_sys_alloc_gpu_buffer(TbRenderSystem *self,
                                     const VkBufferCreateInfo *create_info,
                                     const char *name, TbBuffer *buffer);
VkResult tb_rnd_sys_alloc_gpu_image(TbRenderSystem *self,
                                    const VkImageCreateInfo *create_info,
                                    VmaAllocationCreateFlags vma_flags,
                                    const char *name, TbImage *image);

VkResult tb_rnd_sys_copy_to_tmp_buffer(TbRenderSystem *self, uint64_t size,
                                       uint32_t alignment, const void *data,
                                       uint64_t *offset);
VkResult tb_rnd_sys_copy_to_tmp_buffer2(TbRenderSystem *self, uint64_t size,
                                        uint32_t alignment, uint64_t *offset,
                                        void **ptr);

// Create a GPU buffer and just get a pointer to some mapped memory that
// the caller just needs to fill out.
// An upload will automatically be scheduled if necessary.
// Caller must provide space for a host buffer though it will not be necessary
// on a UMA platform.
VkResult tb_rnd_sys_create_gpu_buffer(TbRenderSystem *self,
                                      const VkBufferCreateInfo *create_info,
                                      const char *name, TbBuffer *buffer,
                                      TbHostBuffer *host, void **ptr);
VkResult tb_rnd_sys_create_gpu_buffer_tmp(TbRenderSystem *self,
                                          const VkBufferCreateInfo *create_info,
                                          const char *name, TbBuffer *buffer,
                                          uint32_t alignment, void **ptr);
// Create a GPU buffer and immediately copy the given data to it.
// An upload will automatically be scheduled if necessary.
// Caller must provide space for a host buffer though it will not be necessary
// on a UMA platform.
VkResult tb_rnd_sys_create_gpu_buffer2(TbRenderSystem *self,
                                       const VkBufferCreateInfo *create_info,
                                       const void *data, const char *name,
                                       TbBuffer *buffer, TbHostBuffer *host);
VkResult tb_rnd_sys_create_gpu_buffer2_tmp(
    TbRenderSystem *self, const VkBufferCreateInfo *create_info,
    const void *data, const char *name, TbBuffer *buffer, uint32_t alignment);

VkResult tb_rnd_sys_create_gpu_image(TbRenderSystem *self, const void *data,
                                     uint64_t data_size,
                                     const VkImageCreateInfo *create_info,
                                     const char *name, TbImage *image,
                                     TbHostBuffer *host);

VkResult tb_rnd_sys_create_gpu_image_tmp(TbRenderSystem *self, const void *data,
                                         uint64_t data_size, uint32_t alignment,
                                         const VkImageCreateInfo *create_info,
                                         const char *name, TbImage *image);

VkBuffer tb_rnd_get_gpu_tmp_buffer(TbRenderSystem *self);

// API for updating the contents of a buffer without resizing it
VkResult tb_rnd_sys_update_gpu_buffer(TbRenderSystem *self,
                                      const TbBuffer *buffer,
                                      const TbHostBuffer *host, void **ptr);

VkResult tb_rnd_create_sampler(TbRenderSystem *self,
                               const VkSamplerCreateInfo *create_info,
                               const char *name, VkSampler *sampler);

VkResult tb_rnd_create_image_view(TbRenderSystem *self,
                                  const VkImageViewCreateInfo *create_info,
                                  const char *name, VkImageView *view);

VkResult tb_rnd_create_buffer_view(TbRenderSystem *self,
                                   const VkBufferViewCreateInfo *create_info,
                                   const char *name, VkBufferView *view);

VkResult
tb_rnd_create_set_layout(TbRenderSystem *self,
                         const VkDescriptorSetLayoutCreateInfo *create_info,
                         const char *name, VkDescriptorSetLayout *set_layout);

VkResult
tb_rnd_create_pipeline_layout(TbRenderSystem *self,
                              const VkPipelineLayoutCreateInfo *create_info,
                              const char *name, VkPipelineLayout *pipe_layout);

VkResult tb_rnd_create_shader(TbRenderSystem *self,
                              const VkShaderModuleCreateInfo *create_info,
                              const char *name, VkShaderModule *shader);

VkResult
tb_rnd_create_descriptor_pool(TbRenderSystem *self,
                              const VkDescriptorPoolCreateInfo *create_info,
                              const char *name, VkDescriptorPool *pool);

VkResult
tb_rnd_create_compute_pipelines(TbRenderSystem *self,
                                uint32_t create_info_count,
                                const VkComputePipelineCreateInfo *create_info,
                                const char *name, VkPipeline *pipelines);
VkResult tb_rnd_create_graphics_pipelines(
    TbRenderSystem *self, uint32_t create_info_count,
    const VkGraphicsPipelineCreateInfo *create_info, const char *name,
    VkPipeline *pipelines);

void tb_rnd_upload_buffers(TbRenderSystem *self, TbBufferCopy *uploads,
                           uint32_t upload_count);
void tb_rnd_upload_buffer_to_image(TbRenderSystem *self,
                                   BufferImageCopy *uploads,
                                   uint32_t upload_count);

void tb_rnd_free_gpu_buffer(TbRenderSystem *self, TbBuffer *buffer);
void tb_rnd_free_gpu_image(TbRenderSystem *self, TbImage *image);

void tb_rnd_destroy_image_view(TbRenderSystem *self, VkImageView view);
void tb_rnd_destroy_sampler(TbRenderSystem *self, VkSampler sampler);
void tb_rnd_destroy_set_layout(TbRenderSystem *self,
                               VkDescriptorSetLayout set_layout);
void tb_rnd_destroy_pipe_layout(TbRenderSystem *self,
                                VkPipelineLayout pipe_layout);
void tb_rnd_destroy_shader(TbRenderSystem *self, VkShaderModule shader);
void tb_rnd_destroy_pipeline(TbRenderSystem *self, VkPipeline pipeline);
void tb_rnd_destroy_descriptor_pool(TbRenderSystem *self,
                                    VkDescriptorPool pool);

void tb_rnd_update_descriptors(TbRenderSystem *self, uint32_t write_count,
                               const VkWriteDescriptorSet *writes);

VkResult tb_rnd_frame_desc_pool_tick(
    TbRenderSystem *self, const VkDescriptorPoolCreateInfo *pool_info,
    const VkDescriptorSetLayout *layouts, void *alloc_next,
    TbFrameDescriptorPool *pools, uint32_t set_count);
VkDescriptorSet tb_rnd_frame_desc_pool_get_set(TbRenderSystem *self,
                                               TbFrameDescriptorPool *pools,
                                               uint32_t set_idx);

VkResult tb_rnd_resize_desc_pool(TbRenderSystem *self,
                                 const VkDescriptorPoolCreateInfo *pool_info,
                                 const VkDescriptorSetLayout *layouts,
                                 void *alloc_next, TbDescriptorPool *pool,
                                 uint32_t set_count);
VkDescriptorSet tb_rnd_desc_pool_get_set(TbDescriptorPool *pool,
                                         uint32_t set_idx);

void tb_flush_alloc(TbRenderSystem *self, VmaAllocation alloc);
