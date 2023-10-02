#pragma once

#define RigidbodyComponentId 0xBAD33333
#define RigidbodyComponentIdStr "0xBAD33333"

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL_stdinc.h>

typedef struct TbWorld TbWorld;

typedef struct TbRigidbodyComponent {
  uint32_t body; // Actually a JPH::BodyId
} TbRigidbodyComponent;

void tb_register_rigidbody_component(TbWorld *world);

#ifdef __cplusplus
}
#endif