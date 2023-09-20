#pragma once

#include "allocator.h"

#define NoClipControllerSystemId 0xFEFEFEFE

typedef struct ecs_world_t ecs_world_t;

// Don't actually need any storage here
typedef uint64_t NoClipControllerSystem;

void tb_register_noclip_sys(ecs_world_t *ecs);
