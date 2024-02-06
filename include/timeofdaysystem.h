#pragma once

#include "allocator.h"

typedef struct TbWorld TbWorld;

typedef struct TbTimeOfDaySystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  float time;
} TbTimeOfDaySystem;

void tb_register_time_of_day_sys(TbWorld *world);
void tb_unregister_time_of_day_sys(TbWorld *world);
