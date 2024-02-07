#pragma once

#include "allocator.h"
#include "tbsystempriority.h"

#define TB_TOD_SYS_PRIO TB_SYSTEM_NORMAL

typedef struct TbWorld TbWorld;

typedef struct TbTimeOfDaySystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  float time;
} TbTimeOfDaySystem;
