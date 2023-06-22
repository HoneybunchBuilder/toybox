#pragma once

#include "allocator.h"

typedef struct SystemDescriptor SystemDescriptor;

#define RotatorSystemId 0x0000BABE

typedef struct RotatorSystemDescriptor {
  Allocator tmp_alloc;
} RotatorSystemDescriptor;

typedef struct RotatorSystem {
  Allocator tmp_alloc;
} RotatorSystem;

void tb_rotator_system_descriptor(SystemDescriptor *desc,
                                  const RotatorSystemDescriptor *rot_desc);
