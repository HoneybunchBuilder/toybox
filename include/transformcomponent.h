#pragma once

#include "simd.h"
#include "world.h"

#define TransformComponentId 0xDEADBEEF

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;

typedef struct TransformComponent {
  bool dirty;
  float4x4 world_matrix;
  Transform transform;
  ecs_entity_t parent;
  uint32_t child_count;
  ecs_entity_t *children;
} TransformComponent;

float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs,
                                       TransformComponent *self);
