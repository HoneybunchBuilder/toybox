#pragma once

#include "allocator.h"
#include "common.hlsli"
#include "rendersystem.h"
#include "tbsystempriority.h"

#include <flecs.h>

#define TB_SHADOW_SYS_PRIO TB_SYSTEM_HIGH

typedef struct TbWorld TbWorld;
typedef uint32_t TbDrawContextId;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;

typedef struct TbShadowSystem TbShadowSystem;
extern ECS_COMPONENT_DECLARE(TbShadowSystem);
