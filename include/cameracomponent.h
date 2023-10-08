#pragma once

// TODO: Physically based camera?

#include "simd.h"

#define CameraComponentId 0xDEADC0DE

typedef struct ecs_world_t ecs_world_t;

typedef uint32_t TbViewId;

typedef struct CameraComponent {
  TbViewId view_id;
  float aspect_ratio;
  float fov;
  float near;
  float far;
  float width;
  float height;
} CameraComponent;

void tb_register_camera_component(ecs_world_t *ecs);
