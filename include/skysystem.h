#pragma once

#include "allocator.h"
#include "tbrendercommon.h"

#define SkySystemId 0xDEADFEED

typedef struct RenderSystem RenderSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct ViewSystem ViewSystem;

typedef uint32_t TbDrawContextId;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_query_t ecs_query_t;

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
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  ecs_query_t *camera_query;

  float time;
  SkySystemFrameState frame_states[TB_MAX_FRAME_STATES];

  TbDrawContextId sky_draw_ctx;
  TbDrawContextId env_capture_ctxs[PREFILTER_PASS_COUNT];
  TbDrawContextId irradiance_ctx;
  TbDrawContextId prefilter_ctxs[PREFILTER_PASS_COUNT];

  VkSampler irradiance_sampler;
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

void tb_register_sky_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                         TbAllocator tmp_alloc);
void tb_unregister_sky_sys(ecs_world_t *ecs);
