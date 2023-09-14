#pragma once

#include "allocator.h"

#define ShadowSystemId 0xB105F00D

typedef struct SystemDescriptor SystemDescriptor;
typedef struct ShadowSystem ShadowSystem;
typedef struct ViewSystem ViewSystem;
typedef struct VisualLoggingSystem VisualLoggingSystem;

typedef struct ShadowSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} ShadowSystemDescriptor;

typedef struct ShadowSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  ViewSystem *view_system;
  VisualLoggingSystem *vlog;
} ShadowSystem;

void tb_shadow_system_descriptor(SystemDescriptor *desc,
                                 const ShadowSystemDescriptor *shadow_desc);

typedef struct ecs_world_t ecs_world_t;
void tb_register_shadow_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc);
void tb_unregister_shadow_sys(ecs_world_t *ecs);
