#pragma once

#include "common.hlsli"
#include "rendertargetsystem.h"
#include "simd.h"

typedef struct ecs_world_t ecs_world_t;
typedef uint32_t TbViewId;

typedef struct TbDirectionalLightComponent {
  float3 color;
  float4 cascade_splits;
  TbViewId cascade_views[TB_CASCADE_COUNT];
} TbDirectionalLightComponent;

void tb_register_light_component(ecs_world_t *ecs);
