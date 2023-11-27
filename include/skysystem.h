#pragma once

#include "tbrendercommon.h"

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbRenderTargetSystem TbRenderTargetSystem;
typedef struct TbViewSystem TbViewSystem;

typedef uint32_t TbDrawContextId;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;

typedef struct TbWorld TbWorld;
typedef struct ecs_query_t ecs_query_t;

typedef struct TbSkySystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} TbSkySystemFrameState;

#define PREFILTER_PASS_COUNT 10

typedef struct TbSkySystem {
  TbRenderSystem *render_system;
  TbRenderPipelineSystem *render_pipe_system;
  TbRenderTargetSystem *render_target_system;
  TbViewSystem *view_system;
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  ecs_query_t *camera_query;

  float time;
  TbSkySystemFrameState frame_states[TB_MAX_FRAME_STATES];

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
} TbSkySystem;

void tb_register_sky_sys(TbWorld* world);
void tb_unregister_sky_sys(TbWorld* world);
