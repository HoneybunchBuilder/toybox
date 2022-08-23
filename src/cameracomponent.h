#pragma once

// TODO: Physically based camera?

#define CameraComponentId 0xDEADFEED

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct CameraComponent {
  float aspect_ratio;
  float fov;
  float near;
  float far;
} CameraComponent;

void tb_camera_component_descriptor(ComponentDescriptor *desc);
