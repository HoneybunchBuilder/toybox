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
  float speed;
  ecs_entity_t camera;
  ecs_entity_t body;
} TbThirdPersonMovementComponent;

void tb_register_third_person_components(TbWorld *world);

#ifdef __cplusplus
}
#endif
