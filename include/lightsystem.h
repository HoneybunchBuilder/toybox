#pragma once

#include "lightcomponent.h"
#include "tbsystempriority.h"

#define TB_LIGHT_SYS_PRIO TB_SYSTEM_HIGH

typedef struct ecs_query_t ecs_query_t;
typedef struct TbWorld TbWorld;

typedef struct TbLightSystem {
  ecs_query_t *dir_light_query;
} TbLightSystem;

void tb_register_light_sys(TbWorld *world);
void tb_unregister_light_sys(TbWorld *world);
