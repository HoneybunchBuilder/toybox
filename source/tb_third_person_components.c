#include "tb_third_person_components.h"

#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_rigidbody_component.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include <flecs.h>
#include <json.h>

typedef struct TbThirdPersonMovementComponentDesc {
  float speed;
  float jump_velocity;
  float distance;
  float angle;
  bool jump;
  bool fixed_rotation;
} TbThirdPersonMovementComponentDesc;

ECS_COMPONENT_DECLARE(TbThirdPersonMovementComponentDesc);
ECS_COMPONENT_DECLARE(TbThirdPersonMovementComponent);

// Third person movement component wants to construct direction vectors to
// the camera that is relevant to the control rig. We also want
// to know what body we want to exert force on
// Need the hierarchy to be constructed first so we look it up in post-load
// Expecting the camera to be attached to some child entity
void post_load_tp_movement(ecs_world_t *ecs, ecs_entity_t ent) {

  tb_auto movement = ecs_get_mut(ecs, ent, TbThirdPersonMovementComponent);

  TB_CHECK(movement->camera == 0, "Didn't expect camera to already be set");

  bool child_camera = false;
  tb_auto child_it = ecs_children(ecs, ent);
  while (ecs_children_next(&child_it)) {
    for (int32_t i = 0; i < child_it.count; ++i) {
      tb_auto child = child_it.entities[i];
      if (ecs_has(ecs, child, TbCameraComponent)) {
        movement->camera = child;
        child_camera = true;
        break;
      }
    }
  }
  TB_CHECK(child_camera, "Didn't find child that has camera");
}

void third_person_move_on_set(ecs_iter_t *it) {
  ecs_world_t *ecs = it->world;
  for (int32_t i = 0; i < it->count; i++) {
    ecs_entity_t ent = it->entities[i];
    if (ecs_has(ecs, ent, TbThirdPersonMovementComponent)) {
      post_load_tp_movement(ecs, ent);
    }
  }
}

bool tb_load_third_person_move_comp(ecs_world_t *ecs, ecs_entity_t ent,
                                    const char *source_path,
                                    const cgltf_data *data,
                                    const cgltf_node *node, json_object *json) {
  (void)source_path;
  (void)data;
  (void)node;

  TbThirdPersonMovementComponent comp = {0};
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "speed") == 0) {
      comp.speed = (float)json_object_get_double(value);
    }
    if (SDL_strcmp(key, "jump") == 0) {
      comp.jump = (bool)json_object_get_boolean(value);
    }
    if (SDL_strcmp(key, "jump_velocity") == 0) {
      comp.jump_velocity = (float)json_object_get_double(value);
    }

    if (SDL_strcmp(key, "fixed_rotation") == 0) {
      comp.fixed_rotation = (bool)json_object_get_boolean(value);
    }
    if (SDL_strcmp(key, "angle") == 0) {
      comp.angle = (float)json_object_get_double(value);
    }
    if (SDL_strcmp(key, "distance") == 0) {
      comp.distance = (float)json_object_get_double(value);
    }
  }
  ecs_set_ptr(ecs, ent, TbThirdPersonMovementComponent, &comp);
  return true;
}

TbComponentRegisterResult tb_register_third_person_move_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbThirdPersonMovementComponentDesc);
  ECS_COMPONENT_DEFINE(ecs, TbThirdPersonMovementComponent);

  ecs_set_hooks(ecs, TbThirdPersonMovementComponent,
                {
                    .on_set = third_person_move_on_set,
                });

  ecs_struct(ecs, {.entity = ecs_id(TbThirdPersonMovementComponentDesc),
                   .members = {
                       {.name = "speed", .type = ecs_id(ecs_f32_t)},
                       {.name = "jump_velocity", .type = ecs_id(ecs_f32_t)},
                       {.name = "distance", .type = ecs_id(ecs_f32_t)},
                       {.name = "angle", .type = ecs_id(ecs_f32_t)},
                       {.name = "jump", .type = ecs_id(ecs_bool_t)},
                       {.name = "fixed_rotation", .type = ecs_id(ecs_bool_t)},
                   }});

  return (TbComponentRegisterResult){
      ecs_id(TbThirdPersonMovementComponent),
      ecs_id(TbThirdPersonMovementComponentDesc)};
}

bool tb_ready_third_person_move_comp(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto comp = ecs_get(ecs, ent, TbThirdPersonMovementComponent);
  return comp != NULL;
}

TB_REGISTER_COMP(tb, third_person_move)
