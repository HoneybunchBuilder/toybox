#pragma once

#include "tb_fxaa.slangh"
#include "tb_render_common.h"
#include "tb_render_pipeline_system.h"

#include <flecs.h>

#define TB_FXAA_SYS_PRIO (TB_RP_SYS_PRIO + 1)

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
  ecs_entity_t shader;
  TbDrawContextId draw_ctx;

  TbFrameDescriptorPoolList pools;
} TbFXAASystem;
extern ECS_COMPONENT_DECLARE(TbFXAASystem);
