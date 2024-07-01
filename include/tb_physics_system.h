#pragma once

#include <flecs.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "tb_simd.h"
#include "tb_system_priority.h"

#define TB_PHYS_SYS_PRIO TB_SYSTEM_HIGH

typedef struct TbPhysicsSystem TbPhysicsSystem;
extern ECS_COMPONENT_DECLARE(TbPhysicsSystem);

typedef struct TbWorld TbWorld;
typedef uint32_t TbRigidbodyComponent;

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;

typedef void (*tb_contact_fn)(ecs_world_t *ecs, ecs_entity_t e1,
                              ecs_entity_t e2);

void tb_register_physics_sys(TbWorld *world);
void tb_unregister_physics_sys(TbWorld *world);

TbRigidbodyComponent tb_phys_copy_body(TbPhysicsSystem *phys_sys,
                                       ecs_entity_t ent,
                                       TbRigidbodyComponent body);

void tb_phys_set_velocity(TbPhysicsSystem *phys_sys, TbRigidbodyComponent body,
                          float3 vel);

void tb_phys_add_contact_callback(TbPhysicsSystem *phys_sys,
                                  ecs_entity_t user_e, tb_contact_fn cb);

#ifdef __cplusplus
}
#endif
