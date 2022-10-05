#pragma once

#include "allocator.h"
#include "renderthread.h"
#include "tbvkalloc.h"
#include "tbvma.h"

#define RenderSystemId 0xABADBABE

#define TB_VMA_TMP_HOST_MB 256

typedef struct SystemDescriptor SystemDescriptor;

typedef struct RenderSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
  RenderThread *render_thread;
} RenderSystemDescriptor;

typedef struct RenderSystemFrameState {
  TbHostBuffer tmp_host_buffer;
  VmaPool tmp_host_pool;

  BufferCopyQueue buf_copy_queue;
  BufferImageCopyQueue buf_img_copy_queue;
} RenderSystemFrameState;

typedef struct FrameDescriptorPool {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} FrameDescriptorPool;

typedef struct RenderSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;
  RenderThread *render_thread;

  VmaPool host_buffer_pool;

  VmaPool gpu_buffer_pool;
  VmaPool gpu_image_pool;

  VkAllocationCallbacks vk_host_alloc_cb;
  VmaAllocator vma_alloc;

  VkPipelineCache pipeline_cache;

  uint32_t frame_idx;
  RenderSystemFrameState frame_states[3];
} RenderSystem;

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc);

VkResult tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                          uint32_t alignment,
                                          TbHostBuffer *buffer);

VkResult tb_rnd_sys_alloc_host_buffer(RenderSystem *self,
                                      const VkBufferCreateInfo *create_info,
                                      const char *name, TbHostBuffer *buffer);
VkResult tb_rnd_sys_alloc_gpu_buffer(RenderSystem *self,
                                     const VkBufferCreateInfo *create_info,
                                     const char *name, TbBuffer *buffer);
VkResult tb_rnd_sys_alloc_gpu_image(RenderSystem *self,
                                    const VkImageCreateInfo *create_info,
                                    const char *name, TbImage *image);

VkBuffer tb_rnd_get_gpu_tmp_buffer(RenderSystem *self);

void tb_rnd_register_pass(RenderSystem *self, VkRenderPass pass,
                          const VkFramebuffer *framebuffers, uint32_t width,
                          uint32_t height, tb_pass_record *record_cb);
void tb_rnd_issue_draw_batch(RenderSystem *self, VkRenderPass pass,
                             uint32_t batch_count, uint64_t batch_size,
                             const void *batches);

VkResult tb_rnd_create_sampler(RenderSystem *self,
                               const VkSamplerCreateInfo *create_info,
                               const char *name, VkSampler *sampler);

VkResult tb_rnd_create_render_pass(RenderSystem *self,
                                   const VkRenderPassCreateInfo *create_info,
                                   const char *name, VkRenderPass *pass);

VkResult tb_rnd_create_framebuffer(RenderSystem *self,
                                   const VkFramebufferCreateInfo *create_info,
                                   const char *name,
                                   VkFramebuffer *framebuffer);

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

VkResult tb_rnd_create_graphics_pipelines(
    RenderSystem *self, uint32_t create_info_count,
    const VkGraphicsPipelineCreateInfo *create_info, const char *name,
    VkPipeline *pipelines);

void tb_rnd_upload_buffers(RenderSystem *self, BufferCopy *uploads,
                           uint32_t upload_count);
void tb_rnd_upload_buffer_to_image(RenderSystem *self, BufferImageCopy *uploads,
                                   uint32_t upload_count);

void tb_rnd_free_gpu_image(RenderSystem *self, TbImage *image);

void tb_rnd_destroy_image_view(RenderSystem *self, VkImageView view);
void tb_rnd_destroy_framebuffer(RenderSystem *self, VkFramebuffer framebuffer);
void tb_rnd_destroy_render_pass(RenderSystem *self, VkRenderPass pass);
void tb_rnd_destroy_sampler(RenderSystem *self, VkSampler sampler);
void tb_rnd_destroy_set_layout(RenderSystem *self,
                               VkDescriptorSetLayout set_layout);
void tb_rnd_destroy_pipe_layout(RenderSystem *self,
                                VkPipelineLayout pipe_layout);
void tb_rnd_destroy_shader(RenderSystem *self, VkShaderModule shader);
void tb_rnd_destroy_pipeline(RenderSystem *self, VkPipeline pipeline);
void tb_rnd_destroy_descriptor_pool(RenderSystem *self, VkDescriptorPool pool);

VkResult
tb_rnd_frame_desc_pool_tick(RenderSystem *self,
                            const VkDescriptorPoolCreateInfo *pool_info,
                            const VkDescriptorSetLayout *layouts,
                            FrameDescriptorPool *pools, uint32_t set_count);
VkDescriptorSet tb_rnd_frame_desc_pool_get_set(RenderSystem *self,
                                               FrameDescriptorPool *pools,
                                               uint32_t set_idx);
