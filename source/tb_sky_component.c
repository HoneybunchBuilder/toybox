#include "tb_sky_component.h"

#include "tb_common.h"
#include "tb_light_component.h"
#include "tb_sky_system.h"
#include "tb_world.h"

#include <json.h>

ECS_COMPONENT_DECLARE(TbSkyDescriptor);
ECS_COMPONENT_DECLARE(TbSkyComponent);

bool tb_load_sky_comp(ecs_world_t *ecs, ecs_entity_t ent,
                      const char *source_path, const cgltf_data *data,
                      const cgltf_node *node, json_object *json) {
  (void)source_path;
  (void)data;
  (void)node;

  TbSkyComponent sky = {0};
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "cirrus") == 0) {
      sky.cirrus = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "cumulus") == 0) {
      sky.cumulus = (float)json_object_get_double(value);
    }
  }

  ecs_set_ptr(ecs, ent, TbSkyComponent, &sky);

  return true;
}

TbComponentRegisterResult tb_register_sky_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbSkyDescriptor);
  ECS_COMPONENT_DEFINE(ecs, TbSkyComponent);

  ecs_struct(ecs, {.entity = ecs_id(TbSkyDescriptor),
                   .members = {
                       {.name = "cirrus", .type = ecs_id(ecs_f32_t)},
                       {.name = "cumulus", .type = ecs_id(ecs_f32_t)},
                   }});
  ecs_struct(ecs, {.entity = ecs_id(TbSkyComponent),
                   .members = {
                       {.name = "time", .type = ecs_id(ecs_f32_t)},
                       {.name = "cirrus", .type = ecs_id(ecs_f32_t)},
                       {.name = "cumulus", .type = ecs_id(ecs_f32_t)},
                   }});

  return (TbComponentRegisterResult){ecs_id(TbSkyComponent),
                                     ecs_id(TbSkyDescriptor)};
}

bool tb_ready_sky_comp(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto sky = ecs_get(ecs, ent, TbSkyComponent);
  return sky != NULL;
}

TB_REGISTER_COMP(tb, sky)
