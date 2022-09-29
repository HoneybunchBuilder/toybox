#pragma once

#include "allocator.h"

#define RenderPipelineSystemId 0xDABBAD00

#define TB_MAX_RENDER_PASS_ATTACHMENTS 4

typedef uint64_t TbTextureId;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct TextureSystem TextureSystem;

typedef struct RenderPipelineSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderPipelineSystemDescriptor;

typedef struct RenderPass RenderPass;

typedef struct RenderPipelineSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  TextureSystem *texture_system;

  uint32_t pass_count;
  RenderPass *render_passes;
  uint32_t pass_max;
} RenderPipelineSystem;

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc);
