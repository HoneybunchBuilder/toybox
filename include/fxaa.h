#pragma once

#include "fxaa.hlsli"
#include "tbrendercommon.h"

typedef struct TbWorld TbWorld;
typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;

typedef uint32_t TbRenderTargetId;
typedef uint32_t TbRenderPassId;
typedef uint32_t TbDrawContextId;

// System API for users to control FXAA settings

typedef struct TbFXAASystem {
  TbRenderTargetId input_target;
  TbRenderTargetId output_target;

  TbFXAAPushConstants settings;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDrawContextId draw_ctx;

  TbFrameDescriptorPoolList pools;
} TbFXAASystem;

void tb_register_fxaa_system(TbWorld *world);
void tb_unregister_fxaa_system(TbWorld *world);
