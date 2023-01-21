#pragma once

#include "allocator.h"
#include "tbrendercommon.h"

#define SkySystemId 0xDEADFEED

typedef struct RenderSystem RenderSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct ViewSystem ViewSystem;
typedef struct SystemDescriptor SystemDescriptor;
typedef uint32_t TbDrawContextId;
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

#define PREFILTER_PASS_COUNT 10

typedef struct SkySystem {
  RenderSystem *render_system;
  RenderPipelineSystem *render_pipe_system;
  RenderTargetSystem *render_target_system;
  ViewSystem *view_system;
  Allocator std_alloc;
  Allocator tmp_alloc;

  SkySystemFrameState frame_states[TB_MAX_FRAME_STATES];

  VkRenderPass sky_pass;
  VkRenderPass env_capture_pass;
  VkRenderPass irradiance_pass;
  VkRenderPass prefilter_passes[PREFILTER_PASS_COUNT];
  TbDrawContextId sky_draw_ctx;
  TbDrawContextId env_capture_ctx;
  TbDrawContextId irradiance_ctx;
  TbDrawContextId prefilter_ctxs[PREFILTER_PASS_COUNT];

  VkSampler sampler;
  VkDescriptorSetLayout sky_set_layout;
  VkPipelineLayout sky_pipe_layout;
  VkDescriptorSetLayout irr_set_layout;
  VkPipelineLayout irr_pipe_layout;
  VkPipelineLayout prefilter_pipe_layout;
  VkPipeline sky_pipeline;
  VkPipeline env_pipeline;
  VkPipeline irradiance_pipeline;
  VkPipeline prefilter_pipeline;

  TbBuffer sky_geom_gpu_buffer;
} SkySystem;

void tb_sky_system_descriptor(SystemDescriptor *desc,
                              const SkySystemDescriptor *sky_desc);
