#pragma once

#include "common.hlsli"

#define TB_BLOOM_MIPS 5

#ifndef __HLSL_VERSION

#include "tb_render_common.h"

typedef uint64_t ecs_entity_t;
typedef struct ecs_world_t ecs_world_t;

typedef uint32_t TbRenderPassId;
typedef uint32_t TbDispatchContextId;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbRenderSystem TbRenderSystem;

typedef struct DownsampleBatch {
  VkDescriptorSet set;
} DownsampleBatch;

typedef struct DownsampleRenderWork {
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  ecs_entity_t shader;
  TbDispatchContextId ctx;
} DownsampleRenderWork;

typedef struct UpsampleBatch {
  VkDescriptorSet set;
} UpsampleBatch;

typedef struct UpsampleRenderWork {
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  ecs_entity_t shader;
  TbDispatchContextId ctx;
} UpsampleRenderWork;

VkResult tb_create_downsample_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                                   TbRenderPipelineSystem *rp_sys,
                                   VkSampler sampler, TbRenderPassId pass,
                                   DownsampleRenderWork *work);
void tb_destroy_downsample_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                                DownsampleRenderWork *work);

VkResult tb_create_upsample_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                                 TbRenderPipelineSystem *rp_sys,
                                 VkSampler sampler, TbRenderPassId pass,
                                 UpsampleRenderWork *work);
void tb_destroy_upsample_work(ecs_world_t *ecs, TbRenderSystem *rnd_sys,
                              UpsampleRenderWork *work);
#endif
