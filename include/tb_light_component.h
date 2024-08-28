#pragma once

#include "common.hlsli"
#include "tb_render_target_system.h"
#include "tb_simd.h"

#include <flecs.h>

typedef struct ecs_world_t ecs_world_t;
typedef uint32_t TbViewId;

typedef struct TbDirectionalLightComponent {
  float3 color;
  float4 cascade_splits;
  TbViewId cascade_views[TB_CASCADE_COUNT];
} TbDirectionalLightComponent;
extern ECS_COMPONENT_DECLARE(TbDirectionalLightComponent);
