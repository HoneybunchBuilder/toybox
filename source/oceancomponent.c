#include "oceancomponent.h"

#include "SDL2/SDL_stdinc.h"
#include "json-c/json_object.h"
#include "json-c/linkhash.h"
#include "world.h"

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

bool deserialize_ocean_component(json_object *json, void *out_desc) {
  OceanComponentDescriptor *desc = (OceanComponentDescriptor *)out_desc;
  desc->wave_count = 1;
  OceanWave *wave = &desc->waves[0];
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "steepness") == 0) {
      wave->steepness = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "wavelength") == 0) {
      wave->wavelength = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "direction_x") == 0) {
      wave->direction[0] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "direction_y") == 0) {
      wave->direction[1] = (float)json_object_get_double(value);
    }
  }
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
      .desc_size = sizeof(OceanComponentDescriptor),
      .id = OceanComponentId,
      .id_str = OceanComponentIdStr,
      .create = tb_create_ocean_component,
      .deserialize = deserialize_ocean_component,
      .destroy = tb_destroy_ocean_component,
  };
}
