#pragma once

#include "common.hlsli"

#define TB_BLOOM_MIPS 4

typedef struct UpsamplePushConstants {
  float radius;
} UpsamplePushConstants;

#ifndef __HLSL_VERSION

#include "tbrendercommon.h"

typedef uint32_t TbRenderPassId;
typedef uint32_t TbDispatchContextId;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct RenderSystem RenderSystem;

typedef struct DownsampleBatch {
  VkDescriptorSet set;
} DownsampleBatch;

typedef struct DownsampleRenderWork {
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} DownsampleRenderWork;

typedef struct UpsampleBatch {
  VkDescriptorSet set;
  UpsamplePushConstants consts;
} UpsampleBatch;

typedef struct UpsampleRenderWork {
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDispatchContextId ctx;
} UpsampleRenderWork;

VkResult create_downsample_work(RenderSystem *render_system,
                                RenderPipelineSystem *render_pipe,
                                VkSampler sampler, TbRenderPassId pass,
                                DownsampleRenderWork *work);
void destroy_downsample_work(RenderSystem *render_system,
                             DownsampleRenderWork *work);

VkResult create_upsample_work(RenderSystem *render_system,
                              RenderPipelineSystem *render_pipe,
                              VkSampler sampler, TbRenderPassId pass,
                              UpsampleRenderWork *work);
void destroy_upsample_work(RenderSystem *render_system,
                           UpsampleRenderWork *work);
#endif
