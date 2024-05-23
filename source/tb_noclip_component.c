#include "tb_noclip_component.h"

#include "json.h"
#include "tb_world.h"

#include <SDL3/SDL_stdinc.h>

ECS_COMPONENT_DECLARE(TbNoClipComponent);

bool tb_load_noclip_comp(TbWorld *world, ecs_entity_t ent,
                         const char *source_path, const cgltf_node *node,
                         json_object *json) {
  (void)source_path;
  (void)node;
  ecs_world_t *ecs = world->ecs;

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

ecs_entity_t tb_register_noclip_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbNoClipComponent);

  ecs_struct(ecs, {.entity = ecs_id(TbNoClipComponent),
                   .members = {
                       {.name = "move_speed", .type = ecs_id(ecs_f32_t)},
                       {.name = "look_speed", .type = ecs_id(ecs_f32_t)},
                   }});
  return ecs_id(TbNoClipComponent);
}

TB_REGISTER_COMP(tb, noclip)
