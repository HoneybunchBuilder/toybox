#pragma once

#include "simd.h"

#define SkyComponentId 0xCAFEB0BA
#define SkyComponentIdStr "0xCAFEB0BA"

typedef struct ecs_world_t ecs_world_t;

typedef struct SkyComponent {
  float time;
  float cirrus;
  float cumulus;
  float3 sun_dir;
} SkyComponent;

void tb_register_sky_component(ecs_world_t *ecs);
