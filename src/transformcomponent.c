#include "transformcomponent.h"

void create_transform_component(TransformComponent *comp,
                                const TransformComponentDescriptor *desc) {
  *comp = (TransformComponent){
      .transform = desc->transform,
  };
}

void destroy_transform_component(TransformComponent *comp) {
  // Setting scale to 0 to implicitly zero out the entire object
  // while avoiding nonsense warnings from the compiler in IDEs
  *comp = (TransformComponent){
      .transform.scale = (float3){0.0f, 0.0f, 0.0f},
  };
}

bool tb_create_transform_component(void *comp, InternalDescriptor desc) {
  create_transform_component((TransformComponent *)comp,
                             (const TransformComponentDescriptor *)desc);
  return true;
}

void tb_destroy_transform_component(void *comp) {
  destroy_transform_component((TransformComponent *)comp);
}

void tb_transform_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Transform";
  desc->size = sizeof(TransformComponent);
  desc->id = TransformComponentId;
  desc->create = tb_create_transform_component;
  desc->destroy = tb_destroy_transform_component;
}
