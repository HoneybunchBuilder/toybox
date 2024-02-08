#pragma once

#include <flecs.h>

#define RigidbodyComponentIdStr "0xBAD33333"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TbWorld TbWorld;

typedef enum TbPhysLayer {
  TB_PHYS_LAYER_STATIC,
  TB_PHYS_LAYER_STATIC_MESH,
  TB_PHYS_LAYER_MOVING,
  TB_PHYS_LAYER_MOVING_MESH,
} TbPhysLayer;
extern ECS_COMPONENT_DECLARE(TbPhysLayer);

typedef enum TbPhysMotionType {
  TB_PHYS_MOTION_STATIC,
  TB_PHYS_MOTION_KINEMATIC,
  TB_PHYS_MOTION_DYNAMIC,
} TbPhysMotionType;
extern ECS_COMPONENT_DECLARE(TbPhysMotionType);

typedef enum TbShapeType {
  TB_PHYS_SHAPE_BOX,
  TB_PHYS_SHAPE_CAPSULE,
  TB_PHYS_SHAPE_CYLINDER,
  TB_PHYS_SHAPE_MESH,
} TbShapeType;
extern ECS_COMPONENT_DECLARE(TbShapeType);

typedef struct TbRigidbodyDescriptor {
  TbPhysLayer layer;
  TbPhysMotionType motion_type;
  TbShapeType shape_type;
  bool sensor;
  // TODO: This should be a bitfield
  bool rot_x;
  bool rot_y;
  bool rot_z;
  bool trans_x;
  bool trans_y;
  bool trans_z;

  float radius;
  float half_height;
  float extent_x;
  float extent_y;
  float extent_z;
} TbRigidbodyDescriptor;
extern ECS_COMPONENT_DECLARE(TbRigidbodyDescriptor);

typedef struct TbRigidbodyComponent {
  uint32_t body; // Actually a JPH::BodyId
} TbRigidbodyComponent;
extern ECS_COMPONENT_DECLARE(TbRigidbodyComponent);

#ifdef __cplusplus
}
#endif
