#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "simd.h"

typedef struct TbPhysicsSystem TbPhysicsSystem;
typedef struct TbWorld TbWorld;
typedef struct TbRigidbodyComponent TbRigidbodyComponent;

void tb_register_physics_sys(TbWorld *world);
void tb_unregister_physics_sys(TbWorld *world);

void tb_phys_add_velocity(TbPhysicsSystem *phys_sys,
                          const TbRigidbodyComponent *body, float3 vel);

#ifdef __cplusplus
}
#endif
