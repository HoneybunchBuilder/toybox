#pragma once

#include "allocator.h"

#define TimeOfDaySystemId 0x1337BEEF

typedef struct ecs_world_t ecs_world_t;

typedef struct TimeOfDaySystem {
  Allocator std_alloc;
  Allocator tmp_alloc;
  float time;
} TimeOfDaySystem;

void tb_register_time_of_day_sys(ecs_world_t *ecs);
void tb_unregister_time_of_day_sys(ecs_world_t *ecs);
