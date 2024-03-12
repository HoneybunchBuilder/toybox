#pragma once

#include "tbsystempriority.h"

#include <flecs.h>

#define TB_TOD_SYS_PRIO TB_SYSTEM_NORMAL

typedef struct TbWorld TbWorld;

typedef struct TbTimeOfDayComponent {
  float time;
  float time_scale;
} TbTimeOfDayComponent;
extern ECS_COMPONENT_DECLARE(TbTimeOfDayComponent);
