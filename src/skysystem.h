#pragma once

#include "allocator.h"
#include "tbrendercommon.h"

#define SkySystemId 0xDEADFEED

typedef struct RenderSystem RenderSystem;
typedef struct SystemDescriptor SystemDescriptor;

typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;

typedef struct SkySystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} SkySystemDescriptor;

typedef struct SkySystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} SkySystemFrameState;

typedef struct SkySystem {
  RenderSystem *render_system;
  Allocator std_alloc;
  Allocator tmp_alloc;

  SkySystemFrameState frame_states[TB_MAX_FRAME_STATES];

  VkRenderPass pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  TbBuffer sky_geom_gpu_buffer;
} SkySystem;

void tb_sky_system_descriptor(SystemDescriptor *desc,
                              const SkySystemDescriptor *sky_desc);
