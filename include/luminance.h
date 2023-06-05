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

typedef struct LuminanceBatch {
  VkDescriptorSet set;
  LuminancePushConstants consts;
} LuminanceBatch;

typedef struct RenderSystem RenderSystem;

void record_luminance_gather(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                             uint32_t batch_count,
                             const DispatchBatch *batches);

VkResult create_lum_gather_set_layout(RenderSystem *render_system,
                                      VkSampler sampler,
                                      VkDescriptorSetLayout *layout);

VkResult create_lum_gather_pipe_layout(RenderSystem *render_system,
                                       VkDescriptorSetLayout set_layout,
                                       VkPipelineLayout *layout);

VkResult create_lum_gather_pipeline(RenderSystem *render_system,
                                    VkPipelineLayout layout,
                                    VkPipeline *pipeline);
#endif
