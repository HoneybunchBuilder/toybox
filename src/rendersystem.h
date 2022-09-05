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
  uint64_t tmp_host_size;
  VmaAllocation tmp_host_alloc;
  VkBuffer tmp_host_buffer;
  uint8_t *tmp_host_mapped;
  VmaPool tmp_host_pool;

  VmaPool gpu_image_pool;

  BufferCopyQueue buf_copy_queue;
  BufferImageCopyQueue buf_img_copy_queue;
} RenderSystemFrameState;

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

typedef struct TbBuffer {
  VkBuffer buffer;
  uint64_t offset;
  void *ptr;
} TbBuffer;

typedef struct TbImage {
  VkImage image;
  VmaAllocation alloc;
  VmaAllocationInfo info;
  void *ptr;
} TbImage;

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc);

VkResult tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                          TbBuffer *buffer);

VkResult tb_rnd_sys_alloc_gpu_image(RenderSystem *self,
                                    const VkImageCreateInfo *create_info,
                                    const char *name, TbImage *image);

VkResult tb_rnd_create_render_pass(RenderSystem *self,
                                   const VkRenderPassCreateInfo *create_info,
                                   const char *name, VkRenderPass *pass);

void tb_rnd_upload_buffers(RenderSystem *self, BufferCopy *uploads,
                           uint32_t upload_count);
void tb_rnd_upload_buffer_to_image(RenderSystem *self, BufferImageCopy *uploads,
                                   uint32_t upload_count);

void tb_rnd_free_gpu_image(RenderSystem *self, TbImage *image);

void tb_rnd_destroy_render_pass(RenderSystem *self, VkRenderPass pass);
void tb_rnd_destroy_sampler(RenderSystem *self, VkSampler sampler);
void tb_rnd_destroy_set_layout(RenderSystem *self,
                               VkDescriptorSetLayout set_layout);
void tb_rnd_destroy_pipe_layout(RenderSystem *self,
                                VkPipelineLayout pipe_layout);
void tb_rnd_destroy_pipeline(RenderSystem *self, VkPipeline pipeline);
