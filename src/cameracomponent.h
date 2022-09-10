#pragma once

// TODO: Physically based camera?

#include "simd.h"

#define CameraComponentId 0xDEADC0DE

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct CameraComponent {
  float aspect_ratio;
  float fov;
  float near;
  float far;
} CameraComponent;

void tb_camera_component_descriptor(ComponentDescriptor *desc);

void tb_camera_view_projection(const CameraComponent *self, float4x4 *vp);
