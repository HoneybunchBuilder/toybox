#pragma once

#include "tb_allocator.h"
#include "tb_bloom.h"
#include "tb_dynarray.h"
#include "tb_luminance.h"
#include "tb_render_common.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_sky_system.h"

#include <flecs.h>

#include <SDL3/SDL_stdinc.h>

typedef ecs_entity_t TbShader;

#define TB_RP_SYS_PRIO (TB_VIEW_SYS_PRIO + 1)

#define TB_MAX_RENDER_PASS_ATTACHMENTS 4

typedef uint32_t TbRenderPassId;
static const TbRenderPassId InvalidRenderPassId = SDL_MAX_UINT32;
typedef uint32_t TbDrawContextId;
static const TbDrawContextId InvalidDrawContextId = SDL_MAX_UINT32;
typedef uint32_t TbDispatchContextId;
static const TbDispatchContextId InvalidDispatchContextId = SDL_MAX_UINT32;

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbRenderTargetSystem TbRenderTargetSystem;
typedef struct TbRenderPass TbRenderPass;
typedef struct TbViewSystem TbViewSystem;

typedef struct TbWorld TbWorld;

typedef struct TbPassAttachment {
  VkClearValue clear_value;
  uint32_t layer;
  uint32_t mip;
  TbRenderTargetId attachment;
} TbPassAttachment;

typedef struct TbDrawContextDescriptor {
  TbRenderPassId pass_id;
  uint64_t batch_size;
  tb_record_draw_batch_fn *draw_fn;
} TbDrawContextDescriptor;
typedef struct TbDrawContext TbDrawContext;

typedef struct TbDispatchContextDescriptor {
  TbRenderPassId pass_id;
  uint64_t batch_size;
  tb_record_dispatch_batch_fn *dispatch_fn;
} TbDispatchContextDescriptor;
typedef struct TbDispatchContext TbDispatchContext;

typedef struct TbRenderPipelineSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;
  TbRenderTargetSystem *rt_sys;
  TbViewSystem *view_sys;

  TbRenderPassId env_cap_passes[PREFILTER_PASS_COUNT];
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
  TbRenderPassId luminance_pass;
  TbRenderPassId brightness_pass;
  TbRenderPassId bloom_blur_pass;
  TbRenderPassId bloom_downsample_pass;
  TbRenderPassId bloom_upsample_pass;
  TbRenderPassId tonemap_pass;
  TbRenderPassId fxaa_pass;
  TbRenderPassId ui_pass;

  TB_DYN_ARR_OF(TbRenderPass) render_passes;
  uint32_t *pass_order; // Array that is kept at the same size as render_passes

  // Some default draw contexts
  TbDrawContextId depth_copy_ctx;
  TbDrawContextId color_copy_ctx;
  TbDrawContextId brightness_ctx;
  TbDrawContextId tonemap_ctx;
  TbDispatchContextId bloom_copy_ctx;
  TbDispatchContextId bloom_blur_ctx;

  // New idea for bundling draw work prims
  DownsampleRenderWork downsample_work;
  UpsampleRenderWork upsample_work;
  TbLumHistRenderWork lum_hist_work;
  TbLumAvgRenderWork lum_avg_work;

  VkSampler sampler;
  VkSampler noise_sampler;
  VkDescriptorSetLayout copy_set_layout;
  VkDescriptorSetLayout comp_copy_set_layout;
  VkDescriptorSetLayout tonemap_set_layout;
  VkPipelineLayout copy_pipe_layout;
  VkPipelineLayout comp_copy_pipe_layout;
  VkPipelineLayout tonemap_pipe_layout;
  TbShader depth_copy_shader;
  TbShader color_copy_shader;
  TbShader brightness_shader;
  TbShader comp_copy_shader;
  TbShader tonemap_shader;

  TbFrameDescriptorPool descriptor_pools[TB_MAX_FRAME_STATES];
  TbFrameDescriptorPool down_desc_pools[TB_MAX_FRAME_STATES];
  TbFrameDescriptorPool up_desc_pools[TB_MAX_FRAME_STATES];
} TbRenderPipelineSystem;
extern ECS_COMPONENT_DECLARE(TbRenderPipelineSystem);

void tb_rnd_on_swapchain_resize(TbRenderPipelineSystem *self);

TbDrawContextId
tb_render_pipeline_register_draw_context(TbRenderPipelineSystem *self,
                                         const TbDrawContextDescriptor *desc);

TbDispatchContextId tb_render_pipeline_register_dispatch_context(
    TbRenderPipelineSystem *self, const TbDispatchContextDescriptor *desc);

void tb_render_pipeline_get_attachments(TbRenderPipelineSystem *self,
                                        TbRenderPassId pass,
                                        uint32_t *attach_count,
                                        TbPassAttachment *attachments);

void tb_render_pipeline_issue_draw_batch(TbRenderPipelineSystem *self,
                                         TbDrawContextId draw_ctx,
                                         uint32_t batch_count,
                                         const TbDrawBatch *batches);

void tb_render_pipeline_issue_dispatch_batch(TbRenderPipelineSystem *self,
                                             TbDispatchContextId dispatch_ctx,
                                             uint32_t batch_count,
                                             const TbDispatchBatch *batches);
