#pragma once

#include "simd.h"
#include "world.h"

#define TransformComponentId 0xDEADBEEF

typedef struct SomeOtherStruct {
  Transform t;
} SomeOtherStruct;

typedef struct TransformComponent {
  Transform transform;
  uint64_t child_count;
  ComponentId *children;
} TransformComponent;

void tb_transform_component_descriptor(ComponentDescriptor *desc);
