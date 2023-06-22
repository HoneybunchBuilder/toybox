#include "transformercomponents.h"

#include "SDL2/SDL_stdinc.h"
#include "json-c/json_object.h"
#include "json-c/linkhash.h"
#include "world.h"

bool create_rotator_component(RotatorComponent *self,
                              const RotatorComponentDescriptor *desc,
                              uint32_t system_dep_count,
                              System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (RotatorComponent){
      .axis = desc->axis,
      .speed = desc->speed,
  };
  return true;
}

bool deserialize_rotator_component(json_object *json, void *out_desc) {
  RotatorComponentDescriptor *desc = (RotatorComponentDescriptor *)out_desc;

  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "axis_x") == 0) {
      desc->axis[0] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "axis_y") == 0) {
      desc->axis[1] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "axis_z") == 0) {
      desc->axis[2] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "speed") == 0) {
      desc->speed = (float)json_object_get_double(value);
    }
  }

  return true;
}

void destroy_rotator_component(RotatorComponent *self,
                               uint32_t system_dep_count,
                               System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (RotatorComponent){0};
}

TB_DEFINE_COMPONENT(rotator, RotatorComponent, RotatorComponentDescriptor)

void tb_rotator_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Rotator",
      .size = sizeof(RotatorComponent),
      .desc_size = sizeof(RotatorComponentDescriptor),
      .id = RotatorComponentId,
      .id_str = RotatorComponentIdStr,
      .create = tb_create_rotator_component,
      .deserialize = deserialize_rotator_component,
      .destroy = tb_destroy_rotator_component,
  };
}
