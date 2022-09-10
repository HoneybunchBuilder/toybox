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

typedef struct SkySystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  VkRenderPass pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  uint32_t sky_set_count;
  VkDescriptorPool sky_pool;
  VkDescriptorSet *sky_sets;
  uint32_t sky_set_max;
} SkySystem;

void tb_sky_system_descriptor(SystemDescriptor *desc,
                              const SkySystemDescriptor *sky_desc);
