#pragma once

#include "allocator.h"
#include "rendersystem.h"

#define OceanSystemId 0xB000DEAD

typedef struct SystemDescriptor SystemDescriptor;
typedef struct MeshSystem MeshSystem;
typedef struct ViewSystem ViewSystem;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef uint64_t TbMeshId;

typedef struct OceanSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} OceanSystemDescriptor;

typedef struct OceanSystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} OceanSystemFrameState;

typedef struct OceanSystem {
  RenderSystem *render_system;
  MeshSystem *mesh_system;
  ViewSystem *view_system;
  Allocator tmp_alloc;
  Allocator std_alloc;

  TbMeshId ocean_patch_mesh;
  uint32_t ocean_index_type;
  uint32_t ocean_index_count;
  uint64_t ocean_pos_offset;
  uint64_t ocean_uv_offset;
  VkBuffer ocean_geom_buffer;

  VkSampler sampler;

  VkRenderPass depth_copy_pass;
  VkFramebuffer depth_copy_pass_framebuffers[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout depth_set_layout;
  VkPipelineLayout depth_pipe_layout;
  VkPipeline depth_copy_pipe;

  VkRenderPass ocean_prepass;
  VkFramebuffer prepass_framebuffers[TB_MAX_FRAME_STATES];

  VkRenderPass ocean_pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  OceanSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline prepass_pipeline;
  VkPipeline pipeline;
} OceanSystem;

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc);
