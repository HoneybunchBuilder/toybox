#pragma once

#include "dynarray.h"
#include "simd.h"
#include "world.h"

#define TransformComponentId 0xDEADBEEF

#ifdef __cplusplus
extern "C" {
#endif

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

const TransformComponent *
tb_transform_get_parent(ecs_world_t *ecs, const TransformComponent *self);
TransformComponent *tb_transform_get_parent_mut(ecs_world_t *ecs,
                                                const TransformComponent *self);
float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs,
                                       TransformComponent *self);
void tb_transform_mark_dirty(ecs_world_t *ecs, TransformComponent *self);
void tb_transform_update(ecs_world_t *ecs, TransformComponent *self,
                         const Transform *trans);

#ifdef __cplusplus
}
#endif
