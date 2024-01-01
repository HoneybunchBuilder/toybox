#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "simd.h"

typedef struct TbPhysicsSystem TbPhysicsSystem;
typedef struct TbWorld TbWorld;
typedef struct TbRigidbodyComponent TbRigidbodyComponent;

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;

typedef void (*tb_contact_fn)(ecs_world_t *ecs, ecs_entity_t e1,
                              ecs_entity_t e2);

void tb_register_physics_sys(TbWorld *world);
void tb_unregister_physics_sys(TbWorld *world);

void tb_phys_add_velocity(TbPhysicsSystem *phys_sys,
                          const TbRigidbodyComponent *body, float3 vel);

void tb_phys_add_contact_callback(TbPhysicsSystem *phys_sys,
                                  ecs_entity_t user_e, tb_contact_fn cb);

#ifdef __cplusplus
}
#endif
