#include "transformcomponent.h"

#include "profiling.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>

const TbTransformComponent *
tb_transform_get_parent(ecs_world_t *ecs, const TbTransformComponent *self) {
  ECS_COMPONENT(ecs, TbTransformComponent);
  const TbTransformComponent *parent_comp = NULL;
  if (self->parent) {
    parent_comp = ecs_get(ecs, self->parent, TbTransformComponent);
  }
  return parent_comp;
}

TbTransformComponent *
tb_transform_get_parent_mut(ecs_world_t *ecs,
                            const TbTransformComponent *self) {
  ECS_COMPONENT(ecs, TbTransformComponent);
  TbTransformComponent *parent_comp = NULL;
  if (self->parent) {
    parent_comp = ecs_get_mut(ecs, self->parent, TbTransformComponent);
  }
  return parent_comp;
}

float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs,
                                       TbTransformComponent *self) {
  TracyCZoneNC(ctx, "TbTransform Get World Matrix", TracyCategoryColorCore,
               true);
  ECS_COMPONENT(ecs, TbTransformComponent);

  if (self->dirty) {
    self->world_matrix = tb_transform_to_matrix(&self->transform);
    // If we have a parent, look up its world transform and combine it with this
    ecs_entity_t parent = self->parent;
    while (parent != InvalidEntityId) {
      TbTransformComponent *parent_comp =
          ecs_get_mut(ecs, parent, TbTransformComponent);
      if (!parent_comp) {
        break;
      }

      float4x4 parent_mat = tb_transform_to_matrix(&parent_comp->transform);
      self->world_matrix = tb_mulf44f44(parent_mat, self->world_matrix);
      parent = parent_comp->parent;
    }
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

  TbTransform world = self->transform;

  ecs_entity_t parent = self->parent;
  while (parent != InvalidEntityId) {
    TbTransformComponent *parent_comp =
        ecs_get_mut(ecs, parent, TbTransformComponent);
    if (!parent_comp) {
      break;
    }

    TbTransform parent_trans = parent_comp->transform;
    world = tb_transform_combine(&world, &parent_trans);
    parent = parent_comp->parent;
  }

  TracyCZoneEnd(ctx);
  return world;
}

void tb_transform_mark_dirty(ecs_world_t *ecs, TbTransformComponent *self) {
  TracyCZoneNC(ctx, "TbTransform Set Dirty", TracyCategoryColorCore, true);
  ECS_COMPONENT(ecs, TbTransformComponent);
  self->dirty = true;
  for (uint32_t i = 0; i < self->child_count; ++i) {
    TbTransformComponent *child_comp =
        ecs_get_mut(ecs, self->children[i], TbTransformComponent);
    tb_transform_mark_dirty(ecs, child_comp);
  }
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
  float4x4 current = tb_transform_get_world_matrix(ecs, self);
  float4x4 world = tb_transform_to_matrix(trans);
  if (tb_f44_eq(&current, &world)) {
    return;
  }

  // Walk the lineage of transforms and collect the inverse of all of them
  TbTransform inv = tb_trans_identity();
  {
    ecs_entity_t p = self->parent;
    while (p != InvalidEntityId) {
      TbTransformComponent *c = ecs_get_mut(ecs, p, TbTransformComponent);
      TbTransform local_inv = tb_inv_trans(c->transform);
      inv = tb_transform_combine(&local_inv, &inv);
      p = c->parent;
    }
  }

  self->transform = tb_transform_combine(trans, &inv);

  // Important so that the renderer knows we've updated the transform
  tb_transform_mark_dirty(ecs, self);
}
