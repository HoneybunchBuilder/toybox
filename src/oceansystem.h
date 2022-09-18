#pragma once

#include "allocator.h"

#define OceanSystemId 0xB000DEAD

typedef struct SystemDescriptor SystemDescriptor;

typedef struct OceanSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} OceanSystemDescriptor;

typedef struct OceanSystem {
  Allocator tmp_alloc;
  Allocator std_alloc;
} OceanSystem;

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc);
