#include "skycomponent.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

bool create_sky_component(SkyComponent *self,
                          const SkyComponentDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  RenderSystem *render_system = tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system, "Failed to get render system reference",
                  false);
  *self = (SkyComponent){
      .render_system = render_system,
  };
  return true;
}

void destroy_sky_component(SkyComponent *self) { *self = (SkyComponent){0}; }

TB_DEFINE_COMPONENT(sky, SkyComponent, SkyComponentDescriptor)

void tb_sky_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Sky",
      .size = sizeof(SkyComponent),
      .id = SkyComponentId,
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_sky_component,
      .destroy = tb_destroy_sky_component,
  };
}
