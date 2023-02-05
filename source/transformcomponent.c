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
              .rotation = (Quaternion){0, 0, 0, 1},
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

TransformComponent *tb_transform_get_parent(TransformComponent *self) {
  EntityId parent = self->parent;
  ComponentStore *store = self->transform_store;
  if (parent == InvalidEntityId && parent & (1 << store->id)) {
    return NULL;
  }
  return &((TransformComponent *)store->components)[parent];
}

float4x4 tb_transform_get_world_matrix(TransformComponent *self) {
  TracyCZoneNC(ctx, "transform component get world matrix",
               TracyCategoryColorCore, true);
  if (self->dirty) {
    transform_to_matrix(&self->world_matrix, &self->transform);
    // If we have a parent, look up its world transform and combine it with this
    if (self->parent != InvalidEntityId) {
      float4x4 parent_mat = mf44_identity();
      TransformComponent *parent_comp = tb_transform_get_parent(self);
      if (parent_comp) {
        parent_mat = tb_transform_get_world_matrix(parent_comp);
        self->world_matrix = mulmf44(parent_mat, self->world_matrix);
      }
    }
    self->dirty = false;
  }
  TracyCZoneEnd(ctx);
  return self->world_matrix;
}
