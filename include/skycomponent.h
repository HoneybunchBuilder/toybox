#pragma once

#include "simd.h"

#include <flecs.h>

typedef struct TbSkyDescriptor {
  float cirrus;
  float cumulus;
} TbSkyDescriptor;
extern ECS_COMPONENT_DECLARE(TbSkyComponent);

typedef struct TbSkyComponent {
  float time;
  float cirrus;
  float cumulus;
  float3 sun_dir;
} TbSkyComponent;
extern ECS_COMPONENT_DECLARE(TbSkyComponent);
