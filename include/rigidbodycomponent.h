#pragma once

#include <flecs.h>

#define RigidbodyComponentIdStr "0xBAD33333"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TbWorld TbWorld;

typedef struct TbRigidbodyComponent {
  uint32_t body; // Actually a JPH::BodyId
} TbRigidbodyComponent;
extern ECS_COMPONENT_DECLARE(TbRigidbodyComponent);

void tb_register_rigidbody_component(TbWorld *world);

#ifdef __cplusplus
}
#endif
