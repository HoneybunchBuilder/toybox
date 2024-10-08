#pragma once

#include "tb_allocator.h"
#include "tb_render_thread.h"
#include "tb_system_priority.h"
#include "tb_vk_alloc.h"
#include "tb_vma.h"

#include <flecs.h>

#define TB_RND_SYS_PRIO TB_SYSTEM_HIGHEST

#define TB_VMA_TMP_HOST_MB 256
#define TB_MAX_LAYERS 16
#define TB_MAX_MIPS 16

typedef struct TbWorld TbWorld;

extern ECS_TAG_DECLARE(TbDescriptorReady);

typedef struct TbRenderSystemFrameState {
  TbHostBuffer tmp_host_buffer;
  TbSetWriteQueue set_write_queue;
  TbBufferCopyQueue buf_copy_queue;
  TbBufferImageCopyQueue buf_img_copy_queue;
} TbRenderSystemFrameState;

typedef struct TbRenderSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  TbRenderThread *render_thread;

  VkAllocationCallbacks vk_host_alloc_cb;
  VmaAllocator vma_alloc;

  VkPipelineCache pipeline_cache;

  uint32_t frame_idx;
  TbRenderSystemFrameState frame_states[3];
} TbRenderSystem;
extern ECS_COMPONENT_DECLARE(TbRenderSystem);

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
// Don't automatically enqueue an upload
VkResult tb_rnd_sys_create_gpu_buffer_noup(
    TbRenderSystem *self, const VkBufferCreateInfo *create_info,
    const char *name, TbBuffer *buffer, TbHostBuffer *host, void **ptr);
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

VkDeviceAddress tb_rnd_get_gpu_tmp_addr(TbRenderSystem *self);

// API for updating the contents of a buffer without resizing it
VkResult tb_rnd_sys_update_gpu_buffer(TbRenderSystem *self,
                                      const TbBuffer *buffer,
                                      const TbHostBuffer *host, void **ptr);

// Updates the GPU buffer with the provided data via the tmp buffer
VkResult tb_rnd_sys_update_gpu_buffer_tmp(TbRenderSystem *self,
                                          const TbBuffer *buffer, void *data,
                                          size_t size, size_t alignment);

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
                                   TbBufferImageCopy *uploads,
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
void tb_rnd_destroy_pipeline(const TbRenderSystem *self, VkPipeline pipeline);
void tb_rnd_destroy_descriptor_pool(TbRenderSystem *self,
                                    VkDescriptorPool pool);

void tb_rnd_update_descriptors(TbRenderSystem *self, uint32_t write_count,
                               const VkWriteDescriptorSet *writes);

VkResult tb_rnd_alloc_descriptor_sets(TbRenderSystem *self, const char *name,
                                      const VkDescriptorSetAllocateInfo *info,
                                      VkDescriptorSet *sets);
VkResult
tb_rnd_frame_desc_pool_tick(TbRenderSystem *self, const char *name,
                            const VkDescriptorPoolCreateInfo *pool_info,
                            const VkDescriptorSetLayout *layouts,
                            void *alloc_next, TbFrameDescriptorPool *pools,
                            uint32_t set_count, uint32_t desc_count);
VkDescriptorSet tb_rnd_frame_desc_pool_get_set(TbRenderSystem *self,
                                               TbFrameDescriptorPool *pools,
                                               uint32_t set_idx);
uint32_t tb_rnd_frame_desc_pool_get_desc_count(TbRenderSystem *self,
                                               TbFrameDescriptorPool *pools);

VkResult tb_rnd_resize_desc_pool(TbRenderSystem *self,
                                 const VkDescriptorPoolCreateInfo *pool_info,
                                 const VkDescriptorSetLayout *layouts,
                                 void *alloc_next, TbDescriptorPool *pool,
                                 uint32_t set_count);
VkDescriptorSet tb_rnd_desc_pool_get_set(TbDescriptorPool *pool,
                                         uint32_t set_idx);

void tb_flush_alloc(TbRenderSystem *self, VmaAllocation alloc);
