#pragma once

// TODO: Physically based camera?

#include "simd.h"

#define CameraComponentId 0xDEADC0DE

typedef uint32_t TbViewId;

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct CameraComponent {
  TbViewId view_id;
  float aspect_ratio;
  float fov;
  float near;
  float far;
} CameraComponent;

void tb_camera_component_descriptor(ComponentDescriptor *desc);

void tb_camera_view_projection(const CameraComponent *self, float4x4 *vp);

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;
typedef struct cgltf_node cgltf_node;
typedef struct json_object json_object;
bool tb_create_camera_component2(ecs_world_t *ecs, ecs_entity_t e,
                                 const char *source_path,
                                 const cgltf_node *node, json_object *extra);
void tb_destroy_camera_component2(ecs_world_t *ecs);
