#include "oceancomponent.h"

#include "world.h"
#include <SDL2/SDL_stdinc.h>

bool create_ocean_component(OceanComponent *comp,
                            const OceanComponentDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (OceanComponent){
      .wave_count = desc->wave_count,
  };
  SDL_memcpy(comp->waves, desc->waves, sizeof(OceanWave) * desc->wave_count);
  return true;
}

void destroy_ocean_component(OceanComponent *comp, uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (OceanComponent){0};
}

TB_DEFINE_COMPONENT(ocean, OceanComponent, OceanComponentDescriptor)

void tb_ocean_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Ocean",
      .size = sizeof(OceanComponent),
      .id = OceanComponentId,
      .create = tb_create_ocean_component,
      .destroy = tb_destroy_ocean_component,
  };
}