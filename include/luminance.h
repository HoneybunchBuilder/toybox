#pragma once

#include "common.hlsli"

typedef struct LuminancePushConstants {
  float4 params;
} LuminancePushConstants;

// If not in a shader, make a quick static assert check
#ifndef __HLSL_VERSION
_Static_assert(sizeof(LuminancePushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

// Declare some functions for the C api
#include "tbrendercommon.h"

typedef uint32_t TbRenderPassId;
typedef uint32_t TbDispatchContextId;
typedef struct RenderPipelineSystem RenderPipelineSystem;

typedef struct LuminanceBatch {
  VkDescriptorSet set;
  LuminancePushConstants consts;
} LuminanceBatch;

typedef struct LumHistRenderWork {
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} LumHistRenderWork;

typedef struct RenderSystem RenderSystem;

VkResult create_lum_hist_work(RenderSystem *render_system,
                              RenderPipelineSystem *render_pipe,
                              VkSampler sampler, TbRenderPassId pass,
                              LumHistRenderWork *work);

void destroy_lum_hist_work(RenderSystem *render_system,
                           LumHistRenderWork *work);
#endif
