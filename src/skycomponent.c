#include "skycomponent.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

bool create_sky_component(SkyComponent *self,
                          const SkyComponentDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (SkyComponent){
      .cirrus = desc->cirrus,
      .cumulus = desc->cumulus,
      .sun_dir = desc->sun_dir,
  };
  return true;
}

void destroy_sky_component(SkyComponent *self, uint32_t system_dep_count,
                           System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (SkyComponent){0};
}

TB_DEFINE_COMPONENT(sky, SkyComponent, SkyComponentDescriptor)

void tb_sky_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Sky",
      .size = sizeof(SkyComponent),
      .id = SkyComponentId,
      .create = tb_create_sky_component,
      .destroy = tb_destroy_sky_component,
  };
}
