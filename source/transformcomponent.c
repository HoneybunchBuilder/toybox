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

  if (self->dirty) {
    self->world_matrix = transform_to_matrix(&self->transform);
    // If we have a parent, look up its world transform and combine it with this
    ecs_entity_t parent = self->parent;
    while (parent != InvalidEntityId) {
      TransformComponent *parent_comp =
          ecs_get_mut(ecs, parent, TransformComponent);
      if (!parent_comp) {
        break;
      }

      float4x4 parent_mat = transform_to_matrix(&parent_comp->transform);
      self->world_matrix = mulmf44(parent_mat, self->world_matrix);
      parent = parent_comp->parent;
    }
    self->dirty = false;
  }

  TracyCZoneEnd(ctx);
  return self->world_matrix;
}

Transform tb_transform_get_world_trans(ecs_world_t *ecs,
                                       const TransformComponent *self) {
  TracyCZoneNC(ctx, "Transform Get World Transform", TracyCategoryColorCore,
               true);
  ECS_COMPONENT(ecs, TransformComponent);

  Transform world = self->transform;

  ecs_entity_t parent = self->parent;
  while (parent != InvalidEntityId) {
    TransformComponent *parent_comp =
        ecs_get_mut(ecs, parent, TransformComponent);
    if (!parent_comp) {
      break;
    }

    Transform parent_trans = parent_comp->transform;
    world = transform_combine(&world, &parent_trans);
    parent = parent_comp->parent;
  }

  TracyCZoneEnd(ctx);
  return world;
}

void tb_transform_mark_dirty(ecs_world_t *ecs, TransformComponent *self) {
  TracyCZoneNC(ctx, "Transform Set Dirty", TracyCategoryColorCore, true);
  ECS_COMPONENT(ecs, TransformComponent);
  self->dirty = true;
  for (uint32_t i = 0; i < self->child_count; ++i) {
    TransformComponent *child_comp =
        ecs_get_mut(ecs, self->children[i], TransformComponent);
    tb_transform_mark_dirty(ecs, child_comp);
  }
  TracyCZoneEnd(ctx);
}

void tb_transform_update(ecs_world_t *ecs, TransformComponent *self,
                         const Transform *trans) {
  if (!tb_transeq(&self->transform, trans)) {
    self->transform = *trans;
    tb_transform_mark_dirty(ecs, self);
  }
}

// Like tb_transform_update except given a world transform we want to compose
// a proper local transform that will align to the given world-space transform
void tb_transform_set_world(ecs_world_t *ecs, TransformComponent *self,
                            const Transform *trans) {
  ECS_COMPONENT(ecs, TransformComponent);

  // If the current, final transform and the given transform match, do nothing
  float4x4 current = tb_transform_get_world_matrix(ecs, self);
  float4x4 world = transform_to_matrix(trans);
  if (tb_mf44eq(&current, &world)) {
    return;
  }

  // Walk the lineage of transforms and collect the inverse of all of them
  Transform inv = trans_identity();
  {
    ecs_entity_t p = self->parent;
    while (p != InvalidEntityId) {
      TransformComponent *c = ecs_get_mut(ecs, p, TransformComponent);
      Transform local_inv = inv_trans(c->transform);
      inv = transform_combine(&local_inv, &inv);
      p = c->parent;
    }
  }

  self->transform = transform_combine(trans, &inv);

  // Important so that the renderer knows we've updated the transform
  tb_transform_mark_dirty(ecs, self);
}
