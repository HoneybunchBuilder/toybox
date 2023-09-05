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
  float3 normal;
} OceanSample;

void tb_ocean_component_descriptor(ComponentDescriptor *desc);

OceanSample tb_sample_ocean(const OceanComponent *ocean,
                            TransformComponent *transform, float2 pos);
