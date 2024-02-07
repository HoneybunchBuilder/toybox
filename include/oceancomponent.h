#pragma once

#include "simd.h"

#include "ocean.hlsli" // Must include simd.h before shader includes

#include <flecs.h>

#define TbOceanComponentIdStr "0xBAD22222"

typedef uint64_t ecs_entity_t;
typedef struct ecs_world_t ecs_world_t;
typedef struct TbWorld TbWorld;
typedef struct TbTransformComponent TbTransformComponent;

typedef struct TbOceanComponent {
  float time;
  uint32_t wave_count;
  TbOceanWave waves[TB_WAVE_MAX];
} TbOceanComponent;
extern ECS_COMPONENT_DECLARE(TbOceanComponent);

typedef struct TbOceanSample {
  float3 pos;
  float3 tangent;
  float3 binormal;
} TbOceanSample;

void tb_register_ocean_component(TbWorld *world);

TbOceanSample tb_sample_ocean(const TbOceanComponent *ocean, ecs_world_t *ecs,
                              ecs_entity_t entity, float2 pos);
