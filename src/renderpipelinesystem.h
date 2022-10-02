#pragma once

#include "allocator.h"
#include <SDL2/SDL_stdinc.h>

#define RenderPipelineSystemId 0xDABBAD00

#define TB_MAX_RENDER_PASS_ATTACHMENTS 4

typedef uint64_t TbTextureId;
typedef uint32_t TbRenderPassId;
static const TbRenderPassId InvalidRenderPassId = SDL_MAX_UINT32;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct TextureSystem TextureSystem;
typedef struct RenderPass RenderPass;
typedef struct VkRenderPass_T *VkRenderPass;

typedef struct RenderPipelineSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderPipelineSystemDescriptor;

typedef struct RenderPipelineSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  TextureSystem *texture_system;

  TbRenderPassId opaque_depth_pass;
  TbRenderPassId opaque_color_pass;
  TbRenderPassId depth_copy_pass;
  TbRenderPassId transparent_color_pass;
  TbRenderPassId ui_pass;

  uint32_t pass_count;
  RenderPass *render_passes;
  uint32_t pass_max;
} RenderPipelineSystem;

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc);

VkRenderPass tb_render_pipeline_get_pass(RenderPipelineSystem *self,
                                         TbRenderPassId pass_id);
