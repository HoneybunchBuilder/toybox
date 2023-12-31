#include "transformcomponent.h"

#include "profiling.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>

float4x4 tb_transform_get_world_matrix(ecs_world_t *ecs, ecs_entity_t entity) {
  TracyCZoneNC(ctx, "TbTransform Get World Matrix", TracyCategoryColorCore,
               true);
  tb_auto comp = ecs_get_mut(ecs, entity, TbTransformComponent);
  if (comp->dirty) {
    comp->world_matrix = tb_transform_to_matrix(&comp->transform);
    // If we have a parent, look up its world transform and combine it with this
    tb_auto parent = ecs_get_parent(ecs, entity);
    while (parent != TbInvalidEntityId) {
      tb_auto parent_comp = ecs_get_mut(ecs, parent, TbTransformComponent);
      if (!parent_comp) {
        break;
      }

      tb_auto parent_mat = tb_transform_to_matrix(&parent_comp->transform);
      comp->world_matrix = tb_mulf44f44(parent_mat, comp->world_matrix);
      parent = ecs_get_parent(ecs, parent);
    }
    comp->dirty = false;
    ecs_modified(ecs, entity, TbTransformComponent);
  }

  TracyCZoneEnd(ctx);
  return comp->world_matrix;
}

TbTransform tb_transform_get_world_trans(ecs_world_t *ecs,
                                         ecs_entity_t entity) {
  TracyCZoneNC(ctx, "TbTransform Get World TbTransform", TracyCategoryColorCore,
               true);

  tb_auto comp = ecs_get(ecs, entity, TbTransformComponent);
  tb_auto world = comp->transform;

  tb_auto parent = ecs_get_parent(ecs, entity);
  while (parent != TbInvalidEntityId) {
    tb_auto parent_comp = ecs_get(ecs, parent, TbTransformComponent);
    if (!parent_comp) {
      break;
    }
    world = tb_transform_combine(&world, &parent_comp->transform);
    parent = ecs_get_parent(ecs, parent);
  }

  TracyCZoneEnd(ctx);
  return world;
}

void tb_transform_mark_dirty(ecs_world_t *ecs, ecs_entity_t entity) {
  TracyCZoneNC(ctx, "TbTransform Set Dirty", TracyCategoryColorCore, true);
  tb_auto comp = ecs_get_mut(ecs, entity, TbTransformComponent);
  comp->dirty = true;
  tb_auto child_it = ecs_children(ecs, entity);
  while (ecs_children_next(&child_it)) {
    for (int i = 0; i < child_it.count; i++) {
      tb_transform_mark_dirty(ecs, child_it.entities[i]);
    }
  }
  ecs_modified(ecs, entity, TbTransformComponent);
  TracyCZoneEnd(ctx);
}

void tb_transform_update(ecs_world_t *ecs, ecs_entity_t entity,
                         const TbTransform *trans) {
  tb_auto comp = ecs_get_mut(ecs, entity, TbTransformComponent);
  if (!tb_trans_eq(&comp->transform, trans)) {
    comp->transform = *trans;
    tb_transform_mark_dirty(ecs, entity);
  }
}

// Like tb_transform_update except given a world transform we want to compose
// a proper local transform that will align to the given world-space transform
void tb_transform_set_world(ecs_world_t *ecs, ecs_entity_t entity,
                            const TbTransform *trans) {

  // If the current, final transform and the given transform match, do nothing
  tb_auto current = tb_transform_get_world_matrix(ecs, entity);
  tb_auto world = tb_transform_to_matrix(trans);
  if (tb_f44_eq(&current, &world)) {
    return;
  }

  // Walk the lineage of transforms and collect the inverse of all of them
  tb_auto inv = tb_trans_identity();
  {
    tb_auto parent = ecs_get_parent(ecs, entity);
    while (parent != TbInvalidEntityId) {
      tb_auto trans_comp = ecs_get(ecs, parent, TbTransformComponent);
      tb_auto local_inv = tb_inv_trans(trans_comp->transform);
      inv = tb_transform_combine(&local_inv, &inv);
      parent = ecs_get_parent(ecs, parent);
    }
  }

  tb_auto comp = ecs_get_mut(ecs, entity, TbTransformComponent);
  comp->transform = tb_transform_combine(trans, &inv);
  // Important so that the renderer knows we've updated the transform
  tb_transform_mark_dirty(ecs, entity);
}
