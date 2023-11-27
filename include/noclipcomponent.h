#pragma once

#include <stdint.h>

#define NoClipComponentId 0xC00010FF
#define NoClipComponentIdStr "0xC00010FF"

typedef struct ecs_world_t ecs_world_t;

typedef struct TbNoClipComponent {
  float move_speed;
  float look_speed;
} TbNoClipComponent;

void tb_register_noclip_component(ecs_world_t *ecs);
