#pragma once

#include "simd.h"

#define DirectionalLightComponentId 0xDECAFBAD

typedef struct ComponentDescriptor ComponentDescriptor;
typedef uint32_t TbViewId;

typedef struct DirectionalLightComponent {
  float3 color;
  float intensity;
  TbViewId view;
} DirectionalLightComponent;

void tb_directional_light_component_descriptor(ComponentDescriptor *desc);
