#pragma once

#include "simd.h"

#include "ocean.hlsli" // Must include simd.h before shader includes

#include <flecs.h>

extern ECS_COMPONENT_DECLARE(TbOceanWave);

typedef struct TbOceanComponent {
  uint32_t wave_count;
  TbOceanWave waves[TB_WAVE_MAX];
} TbOceanComponent;
extern ECS_COMPONENT_DECLARE(TbOceanComponent);

typedef struct TbOceanSample {
  float3 pos;
  float3 tangent;
  float3 binormal;
} TbOceanSample;

TbOceanSample tb_sample_ocean(const TbOceanComponent *ocean, ecs_world_t *ecs,
                              ecs_entity_t entity, float2 pos);
