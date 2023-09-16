#pragma once

#include "lightcomponent.h"

typedef struct ecs_query_t ecs_query_t;
typedef struct ecs_world_t ecs_world_t;

typedef struct LightSysContext {
  ecs_query_t *dir_light_query;
} LightSysContext;

void tb_register_light_sys(ecs_world_t *ecs);
void tb_unregister_light_sys(ecs_world_t *ecs);
