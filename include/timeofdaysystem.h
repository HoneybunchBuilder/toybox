#pragma once

#include "allocator.h"

#define TimeOfDaySystemId 0x1337BEEF

typedef struct SystemDescriptor SystemDescriptor;
typedef struct ViewSystem ViewSystem;

typedef struct TimeOfDaySystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} TimeOfDaySystemDescriptor;

typedef struct TimeOfDaySystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  ViewSystem *view_system;
  float time;
} TimeOfDaySystem;

void tb_time_of_day_system_descriptor(
    SystemDescriptor *desc, const TimeOfDaySystemDescriptor *tod_desc);
