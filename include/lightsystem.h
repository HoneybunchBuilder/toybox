#pragma once

#include "lightcomponent.h"
#include "tbsystempriority.h"

#include <flecs.h>

#define TB_LIGHT_SYS_PRIO TB_SYSTEM_HIGH

typedef struct TbLightSystem {
  ecs_query_t *dir_light_query;
} TbLightSystem;
extern ECS_COMPONENT_DECLARE(TbLightSystem);
