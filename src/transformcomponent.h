#pragma once

#include "simd.h"
#include "world.h"

#define TransformComponentId 0xFF00FF00FF00FF00

typedef struct SomeOtherStruct {
  Transform t;
} SomeOtherStruct;

typedef struct TransformComponent {
  Transform transform;
  uint64_t child_count;
  ComponentId *children;
} TransformComponent;

void tb_transform_component_descriptor(ComponentDescriptor *desc);
