#pragma once

#include "simd.h"

#include <SDL2/SDL_stdinc.h>

#define ThirdPersonMovementComponentIdStr "0xBAD44444"
#define ThirdPersonCameraComponentIdStr "0xBAD55555"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ecs_entity_t;
typedef struct TbWorld TbWorld;

typedef struct TbThirdPersonMovementComponent {
  // Character movement configuration
  ecs_entity_t body;
  float speed;
  bool jump;
  float jump_velocity;

  // Camera configuration
  ecs_entity_t camera;
  bool fixed_rotation;
  float angle;
  float distance;
} TbThirdPersonMovementComponent;

void tb_register_third_person_components(TbWorld *world);

#ifdef __cplusplus
}
#endif
