#include "skycomponent.h"

#include "SDL2/SDL_stdinc.h"
#include "json-c/json_object.h"
#include "json-c/linkhash.h"
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

bool deserialize_sky_component(json_object *json, void *out_desc) {
  SkyComponentDescriptor *desc = (SkyComponentDescriptor *)out_desc;

  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "cirrus") == 0) {
      desc->cirrus = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "cumulus") == 0) {
      desc->cumulus = (float)json_object_get_double(value);
    }
  }

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
      .desc_size = sizeof(SkyComponentDescriptor),
      .id = SkyComponentId,
      .id_str = SkyComponentIdStr,
      .create = tb_create_sky_component,
      .deserialize = deserialize_sky_component,
      .destroy = tb_destroy_sky_component,
  };
}
