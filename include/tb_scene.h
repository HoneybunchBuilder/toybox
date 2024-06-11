#pragma once

#include <flecs.h>

typedef ecs_entity_t TbScene;

TbScene tb_create_scene(ecs_world_t *ecs, const char *scene_path);

bool tb_is_scene_ready(ecs_world_t *ecs, TbScene scene);
