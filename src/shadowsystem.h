#pragma once

#include "allocator.h"

#define ShadowSystemId 0xB105F00D

// TEMP: Make shadow map resolution a user-tunable size
#define TB_SHADOW_MAP_DIM 2048

typedef struct SystemDescriptor SystemDescriptor;
typedef struct ShadowSystem ShadowSystem;
typedef struct RenderSystem RenderSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef uint32_t TbRenderTargetId;

typedef struct ShadowSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} ShadowSystemDescriptor;

typedef struct ShadowSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  RenderTargetSystem *render_target_system;

  TbRenderTargetId shadow_map_target;
} ShadowSystem;

void tb_shadow_system_descriptor(SystemDescriptor *desc,
                                 const ShadowSystemDescriptor *shadow_desc);
