#pragma once

#include "common.hlsli"

typedef struct LuminancePushConstants {
  float4 params;
} LuminancePushConstants;

#ifndef __HLSL_VERSION
_Static_assert(sizeof(LuminancePushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

#include "tbrendercommon.h"

typedef uint32_t TbRenderPassId;
typedef uint32_t TbDispatchContextId;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct RenderSystem RenderSystem;

typedef struct LuminanceBatch {
  VkDescriptorSet set;
  LuminancePushConstants consts;
} LuminanceBatch;

typedef struct LumHistRenderWork {
  TbBuffer lum_histogram;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} LumHistRenderWork;

typedef struct LumAvgRenderWork {
  TbBuffer lum_avg;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} LumAvgRenderWork;

VkResult create_lum_hist_work(RenderSystem *render_system,
                              RenderPipelineSystem *render_pipe,
                              VkSampler sampler, TbRenderPassId pass,
                              LumHistRenderWork *work);

void destroy_lum_hist_work(RenderSystem *render_system,
                           LumHistRenderWork *work);

VkResult create_lum_avg_work(RenderSystem *render_system,
                             RenderPipelineSystem *render_pipe,
                             TbRenderPassId pass, LumAvgRenderWork *work);
void destroy_lum_avg_work(RenderSystem *render_system, LumAvgRenderWork *work);
#endif
