#include "transformcomponent.h"

#include "profiling.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>

const TransformComponent *
tb_transform_get_parent(ecs_world_t *ecs, const TransformComponent *self) {
  ECS_COMPONENT(ecs, TransformComponent);
  const TransformComponent *parent_comp = NULL;
  if (self->parent) {
    parent_comp = ecs_get(ecs, self->parent, TransformComponent);
  }
  return parent_comp;
}

TransformComponent *
tb_transform_get_parent_mut(ecs_world_t *ecs, const TransformComponent *self) {
  ECS_COMPONENT(ecs, TransformComponent);
  TransformComponent *parent_comp = NULL;
  if (self->parent) {
    parent_comp = ecs_get_mut(ecs, self->parent, TransformComponent);
  }
  return parent_comp;
}

float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs,
                                       TransformComponent *self) {
  TracyCZoneNC(ctx, "Transform Get World Matrix", TracyCategoryColorCore, true);
  ECS_COMPONENT(ecs, TransformComponent);

  self->world_matrix = transform_to_matrix(&self->transform);
  // If we have a parent, look up its world transform and combine it with this
  if (self->parent != InvalidEntityId) {
    float4x4 parent_mat = mf44_identity();
    TransformComponent *parent_comp =
        ecs_get_mut(ecs, self->parent, TransformComponent);
    if (parent_comp) {
      parent_mat = tb_transform_get_world_matrix(ecs, parent_comp);
      ecs_modified(ecs, self->parent, TransformComponent);
      self->world_matrix = mulmf44(parent_mat, self->world_matrix);
    }
  }

  TracyCZoneEnd(ctx);
  return self->world_matrix;
}
