#pragma once

#include "allocator.h"
#include "rendersystem.h"

#define OceanSystemId 0xB000DEAD

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;

typedef struct OceanSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} OceanSystemDescriptor;

typedef struct OceanSystem {
  RenderSystem *render_system;
  Allocator tmp_alloc;
  Allocator std_alloc;

  TbHostBuffer host_patch_geom;
  TbBuffer gpu_patch_geom;
} OceanSystem;

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc);
