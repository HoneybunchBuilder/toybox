#include "transformcomponent.h"

void create_transform_component(TransformComponent *comp) {
  *comp = (TransformComponent){
      .transform.scale = (float3){1.0f, 1.0f, 1.0f},
  };
}

void destroy_transform_component(TransformComponent *comp) {
  // Setting scale to 0 to implicitly zero out the entire object
  // while avoiding nonsense warnings from the compiler in IDEs
  *comp = (TransformComponent){
      .transform.scale = (float3){0.0f, 0.0f, 0.0f},
  };
}

bool tb_create_transform_component(void *comp) {
  create_transform_component((TransformComponent *)comp);
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
