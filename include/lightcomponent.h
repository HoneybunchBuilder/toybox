#pragma once

#include "common.hlsli"
#include "rendertargetsystem.h"
#include "simd.h"

#define DirectionalLightComponentId 0xDECAFBAD

typedef struct ComponentDescriptor ComponentDescriptor;
typedef uint32_t TbViewId;

typedef struct DirectionalLightComponent {
  float3 color;
  float4 cascade_splits;
  TbViewId cascade_views[TB_CASCADE_COUNT];
} DirectionalLightComponent;

void tb_directional_light_component_descriptor(ComponentDescriptor *desc);
