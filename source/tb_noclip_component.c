#include "tb_noclip_component.h"

#include "tb_common.h"
#include "tb_world.h"

#include <json.h>

ECS_COMPONENT_DECLARE(TbNoClipComponent);

bool tb_load_noclip_comp(ecs_world_t *ecs, ecs_entity_t ent,
                         const char *source_path, const cgltf_data *data,
                         const cgltf_node *node, json_object *json) {
  (void)source_path;
  (void)data;
  (void)node;
  TbNoClipComponent comp = {0};
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "move_speed") == 0) {
      comp.move_speed = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "look_speed") == 0) {
      comp.look_speed = (float)json_object_get_double(value);
    }
  }
  ecs_set_ptr(ecs, ent, TbNoClipComponent, &comp);
  return true;
}

TbComponentRegisterResult tb_register_noclip_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbNoClipComponent);

  ecs_struct(ecs, {.entity = ecs_id(TbNoClipComponent),
                   .members = {
                       {.name = "move_speed", .type = ecs_id(ecs_f32_t)},
                       {.name = "look_speed", .type = ecs_id(ecs_f32_t)},
                   }});
  return (TbComponentRegisterResult){ecs_id(TbNoClipComponent),
                                     ecs_id(TbNoClipComponent)};
}

bool tb_ready_noclip_comp(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto comp = ecs_get(ecs, ent, TbNoClipComponent);
  return comp != NULL;
}

TB_REGISTER_COMP(tb, noclip)
