#pragma once

#include "common.hlsli"

typedef struct TB_GPU_STRUCT TbLuminancePushConstants {
  float4 params;
} TbLuminancePushConstants;

#ifndef TB_SHADER
_Static_assert(sizeof(TbLuminancePushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

#include "tb_render_common.h"

typedef uint64_t ecs_entity_t;
typedef struct ecs_world_t ecs_world_t;

typedef uint32_t TbRenderPassId;
typedef uint32_t TbDispatchContextId;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbRenderSystem TbRenderSystem;

typedef struct TbLuminanceBatch {
  VkDescriptorSet set;
  TbLuminancePushConstants consts;
} TbLuminanceBatch;

typedef struct TbLumHistRenderWork {
  TbBuffer lum_histogram;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  ecs_entity_t shader;
  TbDispatchContextId ctx;
} TbLumHistRenderWork;

typedef struct TbLumAvgRenderWork {
  TbBuffer lum_avg;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  ecs_entity_t shader;
  TbDispatchContextId ctx;
} TbLumAvgRenderWork;

void tb_create_lum_hist_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                             TbRenderPipelineSystem *rp_sys, VkSampler sampler,
                             TbRenderPassId pass, TbLumHistRenderWork *work);
void tb_destroy_lum_hist_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                              TbLumHistRenderWork *work);

void tb_create_lum_avg_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                            TbRenderPipelineSystem *rp_sys, TbRenderPassId pass,
                            TbLumAvgRenderWork *work);
void tb_destroy_lum_avg_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                             TbLumAvgRenderWork *work);
#endif
