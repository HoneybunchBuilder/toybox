#pragma once

#include "simd.h"
#include "world.h"

#define TransformComponentId 0xDEADBEEF

typedef struct TransformComponentDescriptor {
  Transform transform;
  // TODO: How to specify children?
} TransformComponentDescriptor;

typedef struct TransformComponent {
  bool dirty;
  float4x4 world_matrix;
  Transform transform;
  ComponentId parent;
  uint64_t child_count;
  ComponentId *children;
} TransformComponent;

void tb_transform_component_descriptor(ComponentDescriptor *desc);

void tb_transform_get_world_matrix(TransformComponent *self, float4x4 *world);
