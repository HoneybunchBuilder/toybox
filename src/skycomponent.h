#pragma once

#include "simd.h"

typedef struct RenderSystem RenderSystem;

#define SkyComponentId 0xCAFEB0BA
#define SkyComponentIdStr "0xCAFEB0BA"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct SkyComponentDescriptor {
  float cirrus;
  float cumulus;
  float3 sun_dir;
} SkyComponentDescriptor;

typedef struct SkyComponent {
  float cirrus;
  float cumulus;
  float3 sun_dir; // TODO: decouple this and instead reference this data from a
                  // directional light component
} SkyComponent;

void tb_sky_component_descriptor(ComponentDescriptor *desc);
