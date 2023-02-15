#pragma once

#include "simd.h"
#include "world.h"

#define TransformComponentId 0xDEADBEEF

typedef struct World World;

typedef struct TransformComponentDescriptor {
  World *world;
  EntityId parent;
  Transform transform;
} TransformComponentDescriptor;

typedef struct TransformComponent {
  bool dirty;
  float4x4 world_matrix;
  ComponentStore *transform_store;
  Transform transform;
  EntityId parent;
  uint32_t child_count;
  EntityId *children;
} TransformComponent;

void tb_transform_component_descriptor(ComponentDescriptor *desc);

TransformComponent *tb_transform_get_parent(TransformComponent *self);

float4x4 tb_transform_get_world_matrix(TransformComponent *self);