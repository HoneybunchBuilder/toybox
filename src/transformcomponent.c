#include "transformcomponent.h"

#include "profiling.h"

bool create_transform_component(TransformComponent *comp,
                                const TransformComponentDescriptor *desc,
                                uint32_t system_dep_count,
                                System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (TransformComponent){
      .dirty = true,
      .transform = desc->transform,
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
      .child_count = 0,
      .children = NULL,
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

void tb_transform_get_world_matrix(TransformComponent *self, float4x4 *world) {
  TracyCZoneNC(ctx, "transform component get world matrix",
               TracyCategoryColorCore, true);
  if (self->dirty) {
    transform_to_matrix(&self->world_matrix, &self->transform);
    self->dirty = false;
  }
  *world = self->world_matrix;
  TracyCZoneEnd(ctx);
}
