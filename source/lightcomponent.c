#include "lightcomponent.h"

#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

bool create_directional_light_component(DirectionalLightComponent *comp,
                                        const cgltf_light *desc,
                                        uint32_t system_dep_count,
                                        System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;

  TB_CHECK_RETURN(desc->type == cgltf_light_type_directional,
                  "Creating directional light with incorrect descriptor.",
                  false);

  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);

  *comp = (DirectionalLightComponent){
      .color = {desc->color[0], desc->color[1], desc->color[2]},
  };
  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    comp->cascade_views[i] = tb_view_system_create_view(view_system);
  }
  return true;
}

void destroy_directional_light_component(DirectionalLightComponent *comp,
                                         uint32_t system_dep_count,
                                         System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (DirectionalLightComponent){.color = {0}};
}

TB_DEFINE_COMPONENT(directional_light, DirectionalLightComponent, cgltf_light)

void tb_directional_light_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Directional Light";
  desc->size = sizeof(DirectionalLightComponent);
  desc->system_dep_count = 1;
  desc->system_deps[0] = ViewSystemId;
  desc->id = DirectionalLightComponentId;
  desc->create = tb_create_directional_light_component;
  desc->destroy = tb_destroy_directional_light_component;
}
