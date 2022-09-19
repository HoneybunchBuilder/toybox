#pragma once

#include "allocator.h"
#include "rendersystem.h"

#define OceanSystemId 0xB000DEAD

typedef struct SystemDescriptor SystemDescriptor;
typedef struct MeshSystem MeshSystem;

typedef uint64_t TbMeshId;

typedef struct OceanSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} OceanSystemDescriptor;

typedef struct OceanSystem {
  RenderSystem *render_system;
  MeshSystem *mesh_system;
  Allocator tmp_alloc;
  Allocator std_alloc;

  TbMeshId ocean_patch_mesh;

  VkRenderPass ocean_pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
} OceanSystem;

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc);
