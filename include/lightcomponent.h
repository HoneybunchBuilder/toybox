#pragma once

#include "common.hlsli"
#include "rendertargetsystem.h"
#include "simd.h"

#include <flecs.h>

typedef struct ecs_world_t ecs_world_t;
typedef uint32_t TbViewId;

typedef struct TbDirectionalLightComponent {
  float3 color;
  float4 cascade_splits;
  TbViewId cascade_views[TB_CASCADE_COUNT];
} TbDirectionalLightComponent;
extern ECS_COMPONENT_DECLARE(TbDirectionalLightComponent);
