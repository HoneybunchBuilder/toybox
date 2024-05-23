#pragma once

#include <flecs.h>

typedef struct TbNoClipComponent {
  float move_speed;
  float look_speed;
} TbNoClipComponent;

extern ECS_COMPONENT_DECLARE(TbNoClipComponent);
