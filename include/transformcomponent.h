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

typedef struct TbTransformComponent {
  bool dirty;
  float4x4 world_matrix;
  TbTransform transform;
  ecs_entity_t parent;
  uint32_t child_count;
  ecs_entity_t *children;
} TbTransformComponent;

const TbTransformComponent *
tb_transform_get_parent(ecs_world_t *ecs, const TbTransformComponent *self);
TbTransformComponent *tb_transform_get_parent_mut(ecs_world_t *ecs,
                                                const TbTransformComponent *self);
float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs,
                                       TbTransformComponent *self);
TbTransform tb_transform_get_world_trans(ecs_world_t *ecs,
                                       const TbTransformComponent *self);
void tb_transform_mark_dirty(ecs_world_t *ecs, TbTransformComponent *self);
void tb_transform_update(ecs_world_t *ecs, TbTransformComponent *self,
                         const TbTransform *trans);
void tb_transform_set_world(ecs_world_t *ecs, TbTransformComponent *self,
                            const TbTransform *trans);

#ifdef __cplusplus
}
#endif
