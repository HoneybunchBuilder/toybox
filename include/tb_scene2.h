#pragma once

#include <flecs.h>

typedef ecs_entity_t TbScene2;

TbScene2 tb_load_scene2(ecs_world_t *ecs, const char *scene_path);
