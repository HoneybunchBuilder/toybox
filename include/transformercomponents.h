#pragma once

#include "simd.h"

#define RotatorComponentId 0xBABE0000
#define RotatorComponentIdStr "0xBABE0000"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct RotatorComponentDescriptor {
  float3 axis;
  float speed;
} RotatorComponentDescriptor;

typedef struct RotatorComponent {
  float3 axis;
  float speed;
} RotatorComponent;

void tb_rotator_component_descriptor(ComponentDescriptor *desc);
