#include "tb_common.h"
#include "tb_input_system.h"
#include "tb_physics_system.h"
#include "tb_rigidbody_component.h"
#include "tb_system_priority.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include <json.h>

// Just so that the editor has a structure to generate a UI for
typedef struct TbThrowerDesc {
  bool placeholder;
} TbThrowerDesc;
typedef struct TbThrower {
  ecs_entity_t target_entity;
} TbThrower;
typedef float3 TbThrowDir;
typedef float TbThrowForce;

ECS_COMPONENT_DECLARE(TbThrowerDesc);
ECS_COMPONENT_DECLARE(TbThrowDir);
ECS_COMPONENT_DECLARE(TbThrowForce);
ECS_COMPONENT_DECLARE(TbThrower);

bool tb_load_thrower_comp(ecs_world_t *ecs, ecs_entity_t ent,
                          const char *source_path, const cgltf_data *data,
                          const cgltf_node *node, json_object *json) {
  (void)source_path;
  (void)data;
  (void)node;
  (void)json;
  // Descriptor just marks that a thrower component should be attached
  ecs_set(ecs, ent, TbThrower, {0});
  return true;
}

TbComponentRegisterResult tb_register_thrower_comp(TbWorld *world) {
  tb_auto ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbThrowerDesc);
  ECS_COMPONENT_DEFINE(ecs, TbThrowDir);
  ECS_COMPONENT_DEFINE(ecs, TbThrowForce);
  ECS_COMPONENT_DEFINE(ecs, TbThrower);

  ecs_struct(ecs, {.entity = ecs_id(TbThrowerDesc),
                   .members = {
                       {.name = "placeholder", .type = ecs_id(ecs_bool_t)},

                   }});

  return (TbComponentRegisterResult){ecs_id(TbThrower), ecs_id(TbThrowerDesc)};
}

bool tb_ready_thrower_comp(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto comp = ecs_get(ecs, ent, TbThrower);
  return comp != NULL;
}

TB_REGISTER_COMP(tb, thrower)

void trigger_input(ecs_iter_t *it) {
  tb_auto ecs = it->world;

  tb_auto in_sys = ecs_singleton_get_mut(ecs, TbInputSystem);

  if (in_sys->keyboard.key_space == 0) {
    return;
  }

  tb_auto transforms = ecs_field(it, TbTransformComponent, 1);

  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto ent = it->entities[i];
    tb_auto transform = &transforms[i];

    float3 dir = tb_transform_get_forward(&transform->transform);
    float force = 10;

    ecs_set(ecs, ent, TbThrowDir, {dir.x, dir.y, dir.z});
    ecs_set(ecs, ent, TbThrowForce, {force});
  }
}

void trigger_throwers_sys(ecs_iter_t *it) {
  tb_auto ecs = it->world;
  (void)ecs;

  // tb_auto phys_sys = ecs_singleton_get_mut(ecs, TbPhysicsSystem);
  // tb_auto throwers = ecs_field(it, TbThrower, 1);
  // tb_auto dirs = ecs_field(it, TbThrowDir, 2);
  // tb_auto forces = ecs_field(it, TbThrowForce, 3);
}

void tb_register_thrower_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;

  ECS_SYSTEM(ecs, trigger_input, EcsPostLoad, TbTransformComponent, TbThrower);
  ECS_SYSTEM(ecs, trigger_throwers_sys, EcsPreUpdate, TbThrower, TbThrowDir,
             TbThrowForce);
}

void tb_unregister_thrower_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, thrower, TB_SYSTEM_NORMAL)
