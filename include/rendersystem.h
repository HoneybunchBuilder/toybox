#pragma once

#include "allocator.h"
#include "renderthread.h"
#include "tbvkalloc.h"
#include "tbvma.h"

#define RenderSystemId 0xABADBABE

#define TB_VMA_TMP_HOST_MB 256
#define TB_MAX_LAYERS 16
#define TB_MAX_MIPS 16

typedef struct ecs_world_t ecs_world_t;

typedef struct RenderSystemFrameState {
  TbHostBuffer tmp_host_buffer;

  SetWriteQueue set_write_queue;
  BufferCopyQueue buf_copy_queue;
  BufferImageCopyQueue buf_img_copy_queue;
} RenderSystemFrameState;

typedef struct FrameDescriptorPool {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} FrameDescriptorPool;

typedef struct FrameDescriptorPoolList {
  FrameDescriptorPool pools[TB_MAX_FRAME_STATES];
} FrameDescriptorPoolList;

typedef struct RenderSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;
  RenderThread *render_thread;

  VkAllocationCallbacks vk_host_alloc_cb;
  VmaAllocator vma_alloc;

  VkPipelineCache pipeline_cache;

  uint32_t frame_idx;
  RenderSystemFrameState frame_states[3];
} RenderSystem;

void tb_register_render_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc, RenderThread *render_thread);
void tb_unregister_render_sys(ecs_world_t *ecs);

VkResult tb_rnd_sys_alloc_gpu_buffer(RenderSystem *self,
                                     const VkBufferCreateInfo *create_info,
                                     const char *name, TbBuffer *buffer);
VkResult tb_rnd_sys_alloc_gpu_image(RenderSystem *self,
                                    const VkImageCreateInfo *create_info,
                                    VmaAllocationCreateFlags vma_flags,
                                    const char *name, TbImage *image);

VkResult tb_rnd_sys_tmp_buffer_copy(RenderSystem *self, uint64_t size,
                                    uint32_t alignment, const void *data,
                                    uint64_t *offset);
VkResult tb_rnd_sys_tmp_buffer_get_ptr(RenderSystem *self, uint64_t size,
                                       uint32_t alignment, uint64_t *offset,
                                       void **ptr);

// Create a GPU buffer and just get a pointer to some mapped memory that
// the caller just needs to fill out.
// An upload will automatically be scheduled if necessary.
// Caller must provide space for a host buffer though it will not be necessary
// on a UMA platform.
VkResult tb_rnd_sys_create_gpu_buffer(RenderSystem *self,
                                      const VkBufferCreateInfo *create_info,
                                      const char *name, TbBuffer *buffer,
                                      TbHostBuffer *host, void **ptr);
VkResult tb_rnd_sys_create_gpu_buffer_tmp(RenderSystem *self,
                                          const VkBufferCreateInfo *create_info,
                                          const char *name, TbBuffer *buffer,
                                          uint32_t alignment, void **ptr);
// Create a GPU buffer and immediately copy the given data to it.
// An upload will automatically be scheduled if necessary.
// Caller must provide space for a host buffer though it will not be necessary
// on a UMA platform.
VkResult tb_rnd_sys_create_gpu_buffer2(RenderSystem *self,
                                       const VkBufferCreateInfo *create_info,
                                       const void *data, const char *name,
                                       TbBuffer *buffer, TbHostBuffer *host);
VkResult tb_rnd_sys_create_gpu_buffer2_tmp(
    RenderSystem *self, const VkBufferCreateInfo *create_info, const void *data,
    const char *name, TbBuffer *buffer, uint32_t alignment);

VkResult tb_rnd_sys_create_gpu_image(RenderSystem *self, const void *data,
                                     uint64_t data_size,
                                     const VkImageCreateInfo *create_info,
                                     const char *name, TbImage *image,
                                     TbHostBuffer *host);

VkResult tb_rnd_sys_create_gpu_image_tmp(RenderSystem *self, const void *data,
                                         uint64_t data_size, uint32_t alignment,
                                         const VkImageCreateInfo *create_info,
                                         const char *name, TbImage *image);

VkBuffer tb_rnd_get_gpu_tmp_buffer(RenderSystem *self);

// API for updating the contents of a buffer without resizing it
VkResult tb_rnd_sys_update_gpu_buffer(RenderSystem *self,
                                      const TbBuffer *buffer,
                                      const TbHostBuffer *host, void **ptr);

VkResult tb_rnd_create_sampler(RenderSystem *self,
                               const VkSamplerCreateInfo *create_info,
                               const char *name, VkSampler *sampler);

VkResult tb_rnd_create_image_view(RenderSystem *self,
                                  const VkImageViewCreateInfo *create_info,
                                  const char *name, VkImageView *view);

VkResult
tb_rnd_create_set_layout(RenderSystem *self,
                         const VkDescriptorSetLayoutCreateInfo *create_info,
                         const char *name, VkDescriptorSetLayout *set_layout);

VkResult
tb_rnd_create_pipeline_layout(RenderSystem *self,
                              const VkPipelineLayoutCreateInfo *create_info,
                              const char *name, VkPipelineLayout *pipe_layout);

VkResult tb_rnd_create_shader(RenderSystem *self,
                              const VkShaderModuleCreateInfo *create_info,
                              const char *name, VkShaderModule *shader);

VkResult
tb_rnd_create_descriptor_pool(RenderSystem *self,
                              const VkDescriptorPoolCreateInfo *create_info,
                              const char *name, VkDescriptorPool *pool);

VkResult
tb_rnd_create_compute_pipelines(RenderSystem *self, uint32_t create_info_count,
                                const VkComputePipelineCreateInfo *create_info,
                                const char *name, VkPipeline *pipelines);
VkResult tb_rnd_create_graphics_pipelines(
    RenderSystem *self, uint32_t create_info_count,
    const VkGraphicsPipelineCreateInfo *create_info, const char *name,
    VkPipeline *pipelines);

void tb_rnd_upload_buffers(RenderSystem *self, BufferCopy *uploads,
                           uint32_t upload_count);
void tb_rnd_upload_buffer_to_image(RenderSystem *self, BufferImageCopy *uploads,
                                   uint32_t upload_count);

void tb_rnd_free_gpu_buffer(RenderSystem *self, TbBuffer *buffer);
void tb_rnd_free_gpu_image(RenderSystem *self, TbImage *image);

void tb_rnd_destroy_image_view(RenderSystem *self, VkImageView view);
void tb_rnd_destroy_sampler(RenderSystem *self, VkSampler sampler);
void tb_rnd_destroy_set_layout(RenderSystem *self,
                               VkDescriptorSetLayout set_layout);
void tb_rnd_destroy_pipe_layout(RenderSystem *self,
                                VkPipelineLayout pipe_layout);
void tb_rnd_destroy_shader(RenderSystem *self, VkShaderModule shader);
void tb_rnd_destroy_pipeline(RenderSystem *self, VkPipeline pipeline);
void tb_rnd_destroy_descriptor_pool(RenderSystem *self, VkDescriptorPool pool);

void tb_rnd_update_descriptors(RenderSystem *self, uint32_t write_count,
                               const VkWriteDescriptorSet *writes);

VkResult
tb_rnd_frame_desc_pool_tick(RenderSystem *self,
                            const VkDescriptorPoolCreateInfo *pool_info,
                            const VkDescriptorSetLayout *layouts,
                            FrameDescriptorPool *pools, uint32_t set_count);
VkDescriptorSet tb_rnd_frame_desc_pool_get_set(RenderSystem *self,
                                               FrameDescriptorPool *pools,
                                               uint32_t set_idx);

void tb_flush_alloc(RenderSystem *self, VmaAllocation alloc);
