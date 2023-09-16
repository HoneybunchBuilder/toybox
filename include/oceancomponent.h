#pragma once

#include "simd.h"
#include <stdint.h>

#include "ocean.hlsli" // Must include simd.h before shader includes

#define OceanComponentId 0xBAD22222
#define OceanComponentIdStr "0xBAD22222"

typedef struct ComponentDescriptor ComponentDescriptor;
typedef struct TransformComponent TransformComponent;

typedef struct OceanComponentDescriptor {
  uint32_t wave_count;
  OceanWave waves[TB_WAVE_MAX];
} OceanComponentDescriptor;

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

void tb_ocean_component_descriptor(ComponentDescriptor *desc);

OceanSample tb_sample_ocean(const OceanComponent *ocean,
                            TransformComponent *transform, float2 pos);

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;
typedef struct cgltf_node cgltf_node;
typedef struct json_object json_object;
bool tb_create_ocean_component2(ecs_world_t *ecs, ecs_entity_t e,
                                const char *source_path, const cgltf_node *node,
                                json_object *extra);
void tb_destroy_ocean_components(ecs_world_t *ecs);
