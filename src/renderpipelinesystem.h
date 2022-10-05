#pragma once

#include "allocator.h"
#include "tbrendercommon.h"
#include <SDL2/SDL_stdinc.h>

#define RenderPipelineSystemId 0xDABBAD00

#define TB_MAX_RENDER_PASS_ATTACHMENTS 4

typedef uint64_t TbTextureId;
typedef uint32_t TbRenderPassId;
static const TbRenderPassId InvalidRenderPassId = SDL_MAX_UINT32;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct RenderPass RenderPass;
typedef struct VkRenderPass_T *VkRenderPass;
typedef struct VkFramebuffer_T *VkFramebuffer;

typedef struct DrawContextDescriptor {
  TbRenderPassId pass_id;
  uint64_t batch_size;
  tb_record_draw_batch *draw_fn;
} DrawContextDescriptor;

typedef struct RenderPipelineSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderPipelineSystemDescriptor;

typedef struct RenderPipelineSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  RenderTargetSystem *render_target_system;

  TbRenderPassId opaque_depth_pass;
  TbRenderPassId opaque_color_pass;
  TbRenderPassId depth_copy_pass;
  TbRenderPassId sky_pass;
  TbRenderPassId transparent_depth_pass;
  TbRenderPassId transparent_color_pass;
  TbRenderPassId ui_pass;

  uint32_t pass_count;
  RenderPass *render_passes;
  uint32_t *pass_order;
  uint32_t pass_max;
} RenderPipelineSystem;

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc);

void tb_render_pipeline_register_draw_context(
    RenderPipelineSystem *self, const DrawContextDescriptor *desc);

VkRenderPass tb_render_pipeline_get_pass(RenderPipelineSystem *self,
                                         TbRenderPassId pass_id);

TbRenderPassId tb_render_pipeline_get_ordered_pass(RenderPipelineSystem *self,
                                                   uint32_t idx);
