#pragma once

#include "simd.h"

#include <SDL3/SDL_stdinc.h>
#include <flecs.h>

#define ThirdPersonMovementComponentIdStr "0xBAD44444"
#define ThirdPersonCameraComponentIdStr "0xBAD55555"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ecs_entity_t;
typedef struct TbWorld TbWorld;

typedef struct TbThirdPersonMovementComponent {
  // Character movement configuration
  float speed;
  bool jump;
  float jump_velocity;

  // Camera configuration
  ecs_entity_t camera;
  bool fixed_rotation;
  float angle;
  float distance;
} TbThirdPersonMovementComponent;
extern ECS_COMPONENT_DECLARE(TbThirdPersonMovementComponent);

#ifdef __cplusplus
}
#endif
