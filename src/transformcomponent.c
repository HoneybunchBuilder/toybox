#include "transformcomponent.h"

bool create_transform_component(TransformComponent *comp,
                                const TransformComponentDescriptor *desc) {
  *comp = (TransformComponent){
      .transform = desc->transform,
  };

  return true;
}

void destroy_transform_component(TransformComponent *comp) {
  // Setting scale to 0 to implicitly zero out the entire object
  // while avoiding nonsense warnings from the compiler in IDEs
  *comp = (TransformComponent){
      .transform.scale = (float3){0.0f, 0.0f, 0.0f},
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