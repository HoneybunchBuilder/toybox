#pragma once

// TODO: Physically based camera?

#include "simd.h"

typedef struct TbWorld TbWorld;
typedef uint32_t TbViewId;

typedef struct TbCameraComponent {
  TbViewId view_id;
  float aspect_ratio;
  float fov;
  float near;
  float far;
  float width;
  float height;
} TbCameraComponent;

void tb_register_camera_component(TbWorld *world);
