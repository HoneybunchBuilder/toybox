#pragma once

#include "tb_dynarray.h"
#include "tb_simd.h"

#include <flecs.h>

#ifdef __cplusplus
extern "C" {
#endif

extern ECS_TAG_DECLARE(TbTransformDirty);
typedef struct TbTransformComponent {
  float4x4 world_matrix;
  TbTransform transform;
} TbTransformComponent;
extern ECS_COMPONENT_DECLARE(TbTransformComponent);

float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs, ecs_entity_t entity);
TbTransform tb_transform_get_world_trans(ecs_world_t *ecs, ecs_entity_t entity);
void tb_transform_mark_dirty(ecs_world_t *ecs, ecs_entity_t entity);
void tb_transform_update(ecs_world_t *ecs, ecs_entity_t entity,
                         const TbTransform *trans);
void tb_transform_set_world(ecs_world_t *ecs, ecs_entity_t entity,
                            const TbTransform *trans);

#ifdef __cplusplus
}
#endif
