#include "transformcomponent.h"

#include "profiling.h"
#include "tbcommon.h"
#include "world.h"

bool create_transform_component(TransformComponent *comp,
                                const TransformComponentDescriptor *desc,
                                uint32_t system_dep_count,
                                System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  // Find the transform component store in the world that we need to know about
  ComponentStore *transform_store = NULL;
  for (uint32_t i = 0; i < desc->world->component_store_count; ++i) {
    if (desc->world->component_stores[i].id == TransformComponentId) {
      transform_store = &desc->world->component_stores[i];
      break;
    }
  }
  TB_CHECK_RETURN(transform_store, "Failed to find transform store", false);

  *comp = (TransformComponent){
      .dirty = true,
      .transform_store = transform_store,
      .transform = desc->transform,
      .parent = desc->parent,
  };

  return true;
}

void destroy_transform_component(TransformComponent *comp,
                                 uint32_t system_dep_count,
                                 System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  // Setting scale to 0 to implicitly zero out the entire object
  // while avoiding nonsense warnings from the compiler in IDEs
  *comp = (TransformComponent){
      .transform =
          {
              .scale = (float3){0},
              .rotation = (float3){0},
          },
      .parent = InvalidEntityId,
  };
}

TB_DEFINE_COMPONENT(transform, TransformComponent, TransformComponentDescriptor)

void tb_transform_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Transform";
  desc->size = sizeof(TransformComponent);
  desc->id = TransformComponentId;
  desc->create = tb_create_transform_component;
  desc->destroy = tb_destroy_transform_component;
}

void get_parent_transform_matrix(ComponentStore *store, EntityId parent,
                                 float4x4 *world) {
  TracyCZoneNC(ctx, "transform component get parent matrix",
               TracyCategoryColorCore, true);
  if (parent == InvalidEntityId) {
    TracyCZoneEnd(ctx);
    return;
  }
  TransformComponent *trans_comp =
      &((TransformComponent *)store->components)[parent];
  tb_transform_get_world_matrix(trans_comp, world);

  TracyCZoneEnd(ctx);
}

void tb_transform_get_world_matrix(TransformComponent *self, float4x4 *world) {
  TracyCZoneNC(ctx, "transform component get world matrix",
               TracyCategoryColorCore, true);
  if (self->dirty) {
    transform_to_matrix(&self->world_matrix, &self->transform);
    // If we have a parent, look up its world transform and combine it with this
    if (self->parent != InvalidEntityId) {
      float4x4 parent_mat = {.row0 = {0}};
      mf44_identity(&parent_mat);
      get_parent_transform_matrix(self->transform_store, self->parent,
                                  &parent_mat);
      mulmf44(&parent_mat, &self->world_matrix, &self->world_matrix);
    }
    self->dirty = false;
  }
  *world = self->world_matrix;
  TracyCZoneEnd(ctx);
}
