#include "transformcomponent.h"

#include "profiling.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>

float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs,
                                       TbTransformComponent *self) {
  TracyCZoneNC(ctx, "TbTransform Get World Matrix", TracyCategoryColorCore,
               true);
  ECS_COMPONENT(ecs, TbTransformComponent);

  if (self->dirty) {
    self->world_matrix = tb_transform_to_matrix(&self->transform);
    // If we have a parent, look up its world transform and combine it with this
    tb_auto parent = ecs_get_parent(ecs, self->entity);
    while (parent != TbInvalidEntityId) {
      tb_auto parent_comp = ecs_get_mut(ecs, parent, TbTransformComponent);
      if (!parent_comp) {
        break;
      }

      tb_auto parent_mat = tb_transform_to_matrix(&parent_comp->transform);
      self->world_matrix = tb_mulf44f44(parent_mat, self->world_matrix);
      parent = ecs_get_parent(ecs, parent_comp->entity);
    }
    ecs_modified(ecs, self->entity, TbTransformComponent);
    self->dirty = false;
  }

  TracyCZoneEnd(ctx);
  return self->world_matrix;
}

TbTransform tb_transform_get_world_trans(ecs_world_t *ecs,
                                         const TbTransformComponent *self) {
  TracyCZoneNC(ctx, "TbTransform Get World TbTransform", TracyCategoryColorCore,
               true);
  ECS_COMPONENT(ecs, TbTransformComponent);

  tb_auto world = self->transform;

  tb_auto parent = ecs_get_parent(ecs, self->entity);
  while (parent != TbInvalidEntityId) {
    tb_auto parent_comp = ecs_get_mut(ecs, parent, TbTransformComponent);
    if (!parent_comp) {
      break;
    }
    world = tb_transform_combine(&world, &parent_comp->transform);
    parent = ecs_get_parent(ecs, parent_comp->entity);
  }

  TracyCZoneEnd(ctx);
  return world;
}

void tb_transform_mark_dirty(ecs_world_t *ecs, TbTransformComponent *self) {
  TracyCZoneNC(ctx, "TbTransform Set Dirty", TracyCategoryColorCore, true);
  ECS_COMPONENT(ecs, TbTransformComponent);
  self->dirty = true;
  tb_auto child_it = ecs_children(ecs, self->entity);
  while (ecs_children_next(&child_it)) {
    for (int i = 0; i < child_it.count; i++) {
      tb_auto child_comp =
          ecs_get_mut(ecs, child_it.entities[i], TbTransformComponent);
      tb_transform_mark_dirty(ecs, child_comp);
    }
  }
  ecs_modified(ecs, self->entity, TbTransformComponent);
  TracyCZoneEnd(ctx);
}

void tb_transform_update(ecs_world_t *ecs, TbTransformComponent *self,
                         const TbTransform *trans) {
  if (!tb_trans_eq(&self->transform, trans)) {
    self->transform = *trans;
    tb_transform_mark_dirty(ecs, self);
  }
}

// Like tb_transform_update except given a world transform we want to compose
// a proper local transform that will align to the given world-space transform
void tb_transform_set_world(ecs_world_t *ecs, TbTransformComponent *self,
                            const TbTransform *trans) {
  ECS_COMPONENT(ecs, TbTransformComponent);

  // If the current, final transform and the given transform match, do nothing
  tb_auto current = tb_transform_get_world_matrix(ecs, self);
  tb_auto world = tb_transform_to_matrix(trans);
  if (tb_f44_eq(&current, &world)) {
    return;
  }

  // Walk the lineage of transforms and collect the inverse of all of them
  tb_auto inv = tb_trans_identity();
  {
    tb_auto parent = ecs_get_parent(ecs, self->entity);
    while (parent != TbInvalidEntityId) {
      tb_auto trans_comp = ecs_get_mut(ecs, parent, TbTransformComponent);
      tb_auto local_inv = tb_inv_trans(trans_comp->transform);
      inv = tb_transform_combine(&local_inv, &inv);
      parent = ecs_get_parent(ecs, trans_comp->entity);
    }
  }

  self->transform = tb_transform_combine(trans, &inv);

  // Important so that the renderer knows we've updated the transform
  tb_transform_mark_dirty(ecs, self);
}
