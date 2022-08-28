#include "lightcomponent.h"

#include "tbcommon.h"
#include "tbgltf.h"

#include "world.h"

bool create_directional_light_component(DirectionalLightComponent *comp,
                                        const cgltf_light *desc) {
  TB_CHECK_RETURN(desc->type == cgltf_light_type_directional,
                  "Creating directional light with incorrect descriptor.",
                  false);

  *comp = (DirectionalLightComponent){
      .color = {desc->color[0], desc->color[1], desc->color[2]},
      .intensity = desc->intensity,
  };
  return true;
}

void destroy_directional_light_component(DirectionalLightComponent *comp) {
  *comp = (DirectionalLightComponent){0};
}

TB_DEFINE_COMPONENT(directional_light, DirectionalLightComponent, cgltf_light)

void tb_directional_light_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Directional Light";
  desc->size = sizeof(DirectionalLightComponent);
  desc->id = DirectionalLightComponentId;
  desc->create = tb_create_directional_light_component;
  desc->destroy = tb_destroy_directional_light_component;
}