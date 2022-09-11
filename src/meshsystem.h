#pragma once

#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MeshSystemId 0xBEEFBABE

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;

typedef struct MeshSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} MeshSystemDescriptor;

typedef struct MeshSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  VkRenderPass pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
} MeshSystem;

void tb_mesh_system_descriptor(SystemDescriptor *desc,
                               const MeshSystemDescriptor *mesh_desc);
