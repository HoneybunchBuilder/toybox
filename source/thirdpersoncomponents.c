#include "thirdpersoncomponents.h"

#include "cameracomponent.h"
#include "rigidbodycomponent.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

#include <flecs.h>
#include <json.h>

ECS_COMPONENT_DECLARE(TbThirdPersonMovementComponent);

// Third person movement component wants to construct direction vectors to
// the camera that is relevant to the control rig. We also want
// to know what body we want to exert force on
// Need the hierarchy to be constructed first so we look it up in post-load
// Expecting the camera to be attached to some child entity
void post_load_tp_movement(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto movement = ecs_get_mut(ecs, ent, TbThirdPersonMovementComponent);

  TB_CHECK(movement->body == 0, "Didn't expect body to already be set");
  TB_CHECK(movement->camera == 0, "Didn't expect camera to already be set");

  tb_auto parent = ecs_get_parent(ecs, ent);
  if (parent != TbInvalidEntityId) {
    bool parent_body = ecs_has(ecs, parent, TbRigidbodyComponent);
    if (parent_body) {
      movement->body = parent;
    }
    TB_CHECK(parent_body, "Didn't find parent that has rigidbody");
  }
  {
    bool sibling_camera = false;
    tb_auto child_it = ecs_children(ecs, parent);
    while (ecs_children_next(&child_it)) {
      for (int32_t i = 0; i < child_it.count; ++i) {
        tb_auto sibling = child_it.entities[i];
        if (ecs_has(ecs, sibling, TbCameraComponent)) {
          movement->camera = sibling;
          sibling_camera = true;
          break;
        }
      }
    }
    TB_CHECK(sibling_camera, "Didn't find sibling that has camera");
  }
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

bool tb_load_third_person_move_comp(TbWorld *world, ecs_entity_t ent,
                                    const char *source_path,
                                    const cgltf_node *node, json_object *json) {
  (void)source_path;
  (void)node;

  ecs_world_t *ecs = world->ecs;
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

ecs_entity_t tb_register_third_person_move_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbThirdPersonMovementComponent);

  ecs_set_hooks(ecs, TbThirdPersonMovementComponent,
                {
                    .on_set = third_person_move_on_set,
                });

  return ecs_id(TbThirdPersonMovementComponent);
}

TB_REGISTER_COMP(tb, third_person_move)
