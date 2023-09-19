#pragma once

#include "simd.h"
#include <stdint.h>

#include "ocean.hlsli" // Must include simd.h before shader includes

#define OceanComponentId 0xBAD22222
#define OceanComponentIdStr "0xBAD22222"

typedef struct ecs_world_t ecs_world_t;

typedef struct TransformComponent TransformComponent;

typedef struct OceanComponent {
  float time;
  uint32_t wave_count;
  OceanWave waves[TB_WAVE_MAX];
} OceanComponent;

typedef struct OceanSample {
  float3 pos;
  float3 tangent;
  float3 binormal;
} OceanSample;

void tb_register_ocean_component(ecs_world_t *ecs);

OceanSample tb_sample_ocean(const OceanComponent *ocean, ecs_world_t *ecs,
                            TransformComponent *transform, float2 pos);
