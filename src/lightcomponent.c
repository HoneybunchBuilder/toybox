#include "lightcomponent.h"

#include "tbcommon.h"
#include "tbgltf.h"

#include "world.h"

void create_directional_light_component(DirectionalLightComponent *comp,
                                        const cgltf_light *desc) {
  TB_CHECK(desc->type == cgltf_light_type_directional,
           "Creating directional light with incorrect descriptor.");

  *comp = (DirectionalLightComponent){
      .color = {desc->color[0], desc->color[1], desc->color[2]},
      .intensity = desc->intensity,
  };
}

void destroy_directional_light_component(DirectionalLightComponent *comp) {
  *comp = (DirectionalLightComponent){0};
}

bool tb_create_directional_light_component(void *comp,
                                           InternalDescriptor desc) {
  create_directional_light_component((DirectionalLightComponent *)comp,
                                     (const cgltf_light *)desc);
  return true;
}

void tb_destroy_directional_light_component(void *comp) {
  destroy_directional_light_component((DirectionalLightComponent *)comp);
}

void tb_directional_light_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Directional Light";
  desc->size = sizeof(DirectionalLightComponent);
  desc->id = DirectionalLightComponentId;
  desc->create = tb_create_directional_light_component;
  desc->destroy = tb_destroy_directional_light_component;
}
