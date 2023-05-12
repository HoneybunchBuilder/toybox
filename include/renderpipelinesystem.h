#pragma once

#include "allocator.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "skysystem.h"
#include "tbrendercommon.h"
#include <SDL2/SDL_stdinc.h>

#define RenderPipelineSystemId 0xDABBAD00

#define TB_MAX_RENDER_PASS_ATTACHMENTS 4

typedef uint32_t TbRenderPassId;
static const TbRenderPassId InvalidRenderPassId = SDL_MAX_UINT32;
typedef uint32_t TbDrawContextId;
static const TbDrawContextId InvalidDrawContextId = SDL_MAX_UINT32;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct RenderPass RenderPass;

typedef struct PassAttachment {
  VkClearValue clear_value;
  uint32_t mip;
  TbRenderTargetId attachment;
} PassAttachment;

typedef struct DrawBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;

  VkViewport viewport;
  VkRect2D scissor;

  void *user_batch;

  uint32_t draw_count;
  uint64_t draw_size;
  void *draws;
} DrawBatch;

typedef struct DrawContextDescriptor {
  TbRenderPassId pass_id;
  uint64_t batch_size;
  tb_record_draw_batch *draw_fn;
} DrawContextDescriptor;
typedef struct DrawContext DrawContext;

typedef struct RenderPipelineSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderPipelineSystemDescriptor;

typedef struct RenderPipelineSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  RenderTargetSystem *render_target_system;

  TbRenderPassId env_capture_pass;
  TbRenderPassId irradiance_pass;
  TbRenderPassId prefilter_passes[PREFILTER_PASS_COUNT];
  TbRenderPassId opaque_depth_normal_pass;
  TbRenderPassId opaque_color_pass;
  TbRenderPassId depth_copy_pass;
  TbRenderPassId shadow_passes[TB_CASCADE_COUNT];
  TbRenderPassId color_copy_pass;
  TbRenderPassId sky_pass;
  TbRenderPassId transparent_depth_pass;
  TbRenderPassId transparent_color_pass;
  TbRenderPassId brightness_pass; // Downsamples to quarter res
  TbRenderPassId bloom_blur_x_pass;
  TbRenderPassId bloom_blur_y_pass;
  TbRenderPassId tonemap_pass;
  TbRenderPassId ui_pass;

  uint32_t pass_count;
  RenderPass *render_passes;
  uint32_t *pass_order;
  uint32_t pass_max;

  // Some default draw contexts
  TbDrawContextId depth_copy_ctx;
  TbDrawContextId color_copy_ctx;
  TbDrawContextId brightness_ctx;
  TbDrawContextId bloom_blur_x_ctx;
  TbDrawContextId bloom_blur_y_ctx;
  TbDrawContextId tonemap_ctx;

  // Copy resources
  VkSampler sampler;
  VkDescriptorSetLayout copy_set_layout;
  VkPipelineLayout bloom_blur_layout;
  VkPipelineLayout copy_pipe_layout;
  VkPipeline depth_copy_pipe;
  VkPipeline color_copy_pipe;
  VkPipeline brightness_pipe;
  VkPipeline bloom_blur_pipe; // used for blurring along X and Y axes
  VkPipeline tonemap_pipe;

  FrameDescriptorPool descriptor_pools[TB_MAX_FRAME_STATES];
} RenderPipelineSystem;

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc);

void tb_rnd_on_swapchain_resize(RenderPipelineSystem *self);

TbDrawContextId
tb_render_pipeline_register_draw_context(RenderPipelineSystem *self,
                                         const DrawContextDescriptor *desc);

void tb_render_pipeline_get_attachments(RenderPipelineSystem *self,
                                        TbRenderPassId pass,
                                        uint32_t *attach_count,
                                        PassAttachment *attachments);

void tb_render_pipeline_issue_draw_batch(RenderPipelineSystem *self,
                                         TbDrawContextId draw_ctx,
                                         uint32_t batch_count,
                                         const DrawBatch *batches);
