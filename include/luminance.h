#pragma once

#include "common.hlsli"

typedef struct TB_GPU_STRUCT TbLuminancePushConstants {
  float4 params;
} TbLuminancePushConstants;

#ifndef __HLSL_VERSION
_Static_assert(sizeof(TbLuminancePushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

#include "tbrendercommon.h"

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
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} TbLumHistRenderWork;

typedef struct TbLumAvgRenderWork {
  TbBuffer lum_avg;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} TbLumAvgRenderWork;

VkResult tb_create_lum_hist_work(TbRenderSystem *rnd_sys,
                                 TbRenderPipelineSystem *rp_sys,
                                 VkSampler sampler, TbRenderPassId pass,
                                 TbLumHistRenderWork *work);

void tb_destroy_lum_hist_work(TbRenderSystem *rnd_sys,
                              TbLumHistRenderWork *work);

VkResult tb_create_lum_avg_work(TbRenderSystem *rnd_sys,
                                TbRenderPipelineSystem *rp_sys,
                                TbRenderPassId pass, TbLumAvgRenderWork *work);
void tb_destroy_lum_avg_work(TbRenderSystem *rnd_sys,
                             TbLumAvgRenderWork *work);
#endif
