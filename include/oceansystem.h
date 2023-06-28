#pragma once

#include "allocator.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"

#define OceanSystemId 0xB000DEAD

typedef struct SystemDescriptor SystemDescriptor;
typedef struct MeshSystem MeshSystem;
typedef struct ViewSystem ViewSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct VisualLoggingSystem VisualLoggingSystem;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef uint64_t TbMeshId;
typedef uint32_t TbDrawContextId;

typedef struct OceanSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} OceanSystemDescriptor;

typedef struct OceanSystem {
  RenderSystem *render_system;
  RenderPipelineSystem *render_pipe_system;
  MeshSystem *mesh_system;
  ViewSystem *view_system;
  RenderTargetSystem *render_target_system;
  VisualLoggingSystem *vlog;
  Allocator tmp_alloc;
  Allocator std_alloc;

  TbMeshId ocean_patch_mesh;
  Transform ocean_transform;
  float tile_width;
  float tile_depth;
  uint32_t ocean_index_type;
  uint32_t ocean_index_count;
  uint64_t ocean_pos_offset;
  uint64_t ocean_uv_offset;
  VkBuffer ocean_geom_buffer;

  VkSampler sampler;

  TbDrawContextId trans_depth_draw_ctx;
  TbDrawContextId shadow_draw_ctx;
  TbDrawContextId trans_color_draw_ctx;

  FrameDescriptorPool ocean_pools[TB_MAX_FRAME_STATES];

  VkPipelineLayout shadow_pipe_layout;
  VkPipeline shadow_pipeline;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline prepass_pipeline;
  VkPipeline pipeline;
} OceanSystem;

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc);
