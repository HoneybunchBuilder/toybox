#pragma once

#include "simd.h"
#include "world.h"

#define TransformComponentId 0xDEADBEEF

typedef struct TransformComponentDescriptor {
  Transform transform;
  // TODO: How to specify children?
} TransformComponentDescriptor;

typedef struct TransformComponent {
  Transform transform;
  uint64_t child_count;
  ComponentId *children;
} TransformComponent;

void tb_transform_component_descriptor(ComponentDescriptor *desc);
