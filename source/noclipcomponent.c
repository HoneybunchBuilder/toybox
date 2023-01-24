#include "noclipcomponent.h"

#include "SDL2/SDL_stdinc.h"
#include "json-c/json_object.h"
#include "json-c/linkhash.h"
#include "world.h"

bool create_noclip_component(NoClipComponent *comp,
                             const NoClipComponentDescriptor *desc,
                             uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (NoClipComponent){
      .move_speed = desc->move_speed,
      .look_speed = desc->look_speed,
  };
  return true;
}

bool deserialize_noclip_component(json_object *json, void *out_desc) {
  NoClipComponentDescriptor *desc = (NoClipComponentDescriptor *)out_desc;

  // Find move_speed and look_speed
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "move_speed") == 0) {
      desc->move_speed = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "look_speed") == 0) {
      desc->look_speed = (float)json_object_get_double(value);
    }
  }

  return true;
}

void destroy_noclip_component(NoClipComponent *comp, uint32_t system_dep_count,
                              System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (NoClipComponent){0};
}

TB_DEFINE_COMPONENT(noclip, NoClipComponent, NoClipComponentDescriptor)

void tb_noclip_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "NoClip",
      .size = sizeof(NoClipComponent),
      .desc_size = sizeof(NoClipComponentDescriptor),
      .id = NoClipComponentId,
      .id_str = NoClipComponentIdStr,
      .create = tb_create_noclip_component,
      .deserialize = deserialize_noclip_component,
      .destroy = tb_destroy_noclip_component,
  };
}
