#pragma once

#include "allocator.h"

#define ShadowSystemId 0xB105F00D

typedef struct ViewSystem ViewSystem;
typedef struct VisualLoggingSystem VisualLoggingSystem;
typedef struct TbWorld TbWorld;

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_query_t ecs_query_t;

typedef struct ShadowSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  ecs_query_t *dir_light_query;
} ShadowSystem;

void tb_register_shadow_sys(TbWorld *world);
void tb_unregister_shadow_sys(ecs_world_t *ecs);
