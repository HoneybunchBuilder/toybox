#pragma once

#include "simd.h"
#include <stdint.h>

#include "ocean.hlsli" // Must include simd.h before shader includes

#define OceanComponentId 0xBAD22222
#define OceanComponentIdStr "0xBAD22222"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct OceanComponentDescriptor {
  uint32_t wave_count;
  OceanWave waves[TB_WAVE_MAX];
} OceanComponentDescriptor;

typedef struct OceanComponent {
  float time;
  uint32_t wave_count;
  OceanWave waves[TB_WAVE_MAX];
} OceanComponent;

void tb_ocean_component_descriptor(ComponentDescriptor *desc);
