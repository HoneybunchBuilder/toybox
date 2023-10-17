#pragma once

#include "allocator.h"
#include "bloom.h"
#include "dynarray.h"
#include "luminance.h"
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
typedef uint32_t TbDispatchContextId;
static const TbDispatchContextId InvalidDispatchContextId = SDL_MAX_UINT32;

typedef struct RenderSystem RenderSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct RenderPass RenderPass;
typedef struct ViewSystem ViewSystem;

typedef struct ecs_world_t ecs_world_t;

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

#define MAX_GROUPS 8
typedef struct DispatchBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;
  void *user_batch;
  uint32_t group_count;
  uint3 groups[MAX_GROUPS];
} DispatchBatch;

typedef TB_DYN_ARR_OF(DrawBatch) DrawBatchList;
typedef TB_DYN_ARR_OF(DispatchBatch) DispatchBatchList;

typedef struct DrawContextDescriptor {
  TbRenderPassId pass_id;
  uint64_t batch_size;
  tb_record_draw_batch *draw_fn;
} DrawContextDescriptor;
typedef struct DrawContext DrawContext;

typedef struct DispatchContextDescriptor {
  TbRenderPassId pass_id;
  uint64_t batch_size;
  tb_record_dispatch_batch *dispatch_fn;
} DispatchContextDescriptor;
typedef struct DispatchContext DispatchContext;

typedef struct RenderPipelineSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  RenderTargetSystem *render_target_system;
  ViewSystem *view_system;

  TbRenderPassId env_cap_passes[PREFILTER_PASS_COUNT];
  TbRenderPassId irradiance_pass;
  TbRenderPassId prefilter_passes[PREFILTER_PASS_COUNT];
  TbRenderPassId opaque_depth_normal_pass;
  TbRenderPassId ssao_pass;
  TbRenderPassId ssao_blur_pass;
  TbRenderPassId opaque_color_pass;
  TbRenderPassId depth_copy_pass;
  TbRenderPassId shadow_pass;
  TbRenderPassId color_copy_pass;
  TbRenderPassId sky_pass;
  TbRenderPassId transparent_depth_pass;
  TbRenderPassId transparent_color_pass;
  TbRenderPassId luminance_pass;
  TbRenderPassId brightness_pass;
  TbRenderPassId bloom_blur_pass;
  TbRenderPassId bloom_downsample_pass;
  TbRenderPassId bloom_upsample_pass;
  TbRenderPassId tonemap_pass;
  TbRenderPassId ui_pass;

  TB_DYN_ARR_OF(RenderPass) render_passes;
  uint32_t *pass_order; // Array that is kept at the same size as render_passes

  TbBuffer ssao_params;
  TbImage ssao_noise;
  VkImageView ssao_noise_view;

  // Some default draw contexts
  TbDrawContextId ssao_ctx;
  TbDrawContextId depth_copy_ctx;
  TbDrawContextId color_copy_ctx;
  TbDrawContextId brightness_ctx;
  TbDrawContextId tonemap_ctx;
  TbDispatchContextId bloom_copy_ctx;
  TbDispatchContextId bloom_blur_ctx;
  TbDispatchContextId ssao_blur_ctx;

  // New idea for bundling draw work prims
  DownsampleRenderWork downsample_work;
  UpsampleRenderWork upsample_work;
  LumHistRenderWork lum_hist_work;
  LumAvgRenderWork lum_avg_work;

  VkSampler sampler;
  VkSampler noise_sampler;
  VkDescriptorSetLayout ssao_set_layout;
  VkDescriptorSetLayout blur_set_layout;
  VkDescriptorSetLayout copy_set_layout;
  VkDescriptorSetLayout comp_copy_set_layout;
  VkDescriptorSetLayout tonemap_set_layout;
  VkPipelineLayout ssao_pipe_layout;
  VkPipelineLayout blur_pipe_layout;
  VkPipelineLayout copy_pipe_layout;
  VkPipelineLayout comp_copy_pipe_layout;
  VkPipelineLayout tonemap_pipe_layout;
  VkPipeline ssao_pipe;
  VkPipeline blur_h_pipe;
  VkPipeline blur_v_pipe;
  VkPipeline depth_copy_pipe;
  VkPipeline color_copy_pipe;
  VkPipeline brightness_pipe;
  VkPipeline comp_copy_pipe;
  VkPipeline tonemap_pipe;

  FrameDescriptorPool descriptor_pools[TB_MAX_FRAME_STATES];
} RenderPipelineSystem;

void tb_register_render_pipeline_sys(ecs_world_t *ecs, Allocator std_alloc,
                                     Allocator tmp_alloc);
void tb_unregister_render_pipeline_sys(ecs_world_t *ecs);

void tb_rnd_on_swapchain_resize(RenderPipelineSystem *self);

TbDrawContextId
tb_render_pipeline_register_draw_context(RenderPipelineSystem *self,
                                         const DrawContextDescriptor *desc);

TbDispatchContextId tb_render_pipeline_register_dispatch_context(
    RenderPipelineSystem *self, const DispatchContextDescriptor *desc);

void tb_render_pipeline_get_attachments(RenderPipelineSystem *self,
                                        TbRenderPassId pass,
                                        uint32_t *attach_count,
                                        PassAttachment *attachments);

void tb_render_pipeline_issue_draw_batch(RenderPipelineSystem *self,
                                         TbDrawContextId draw_ctx,
                                         uint32_t batch_count,
                                         const DrawBatch *batches);

void tb_render_pipeline_issue_dispatch_batch(RenderPipelineSystem *self,
                                             TbDispatchContextId dispatch_ctx,
                                             uint32_t batch_count,
                                             const DispatchBatch *batches);
