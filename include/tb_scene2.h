#pragma once

#include <flecs.h>

typedef ecs_entity_t TbScene2;

TbScene2 tb_load_scene2(ecs_world_t *ecs, const char *scene_path);

bool tb_is_scene_ready(ecs_world_t *ecs, TbScene2 scene);
