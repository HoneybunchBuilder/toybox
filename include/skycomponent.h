#pragma once

#include "simd.h"

#define TbSkyComponentIdStr "0xCAFEB0BA"

typedef struct TbWorld TbWorld;

typedef struct TbSkyComponent {
  float time;
  float cirrus;
  float cumulus;
  float3 sun_dir;
} TbSkyComponent;

void tb_register_sky_component(TbWorld *world);
