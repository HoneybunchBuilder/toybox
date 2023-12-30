#include "thirdpersoncomponents.h"

#include "assetsystem.h"
#include "cameracomponent.h"
#include "rigidbodycomponent.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

#include <flecs.h>
#include <json.h>

bool try_attach_tp_move_comp(ecs_world_t *ecs, ecs_entity_t e,
                             json_object *json) {
  {
    bool has_comp = false;

    json_object_object_foreach(json, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, ThirdPersonMovementComponentIdStr) == 0) {
          has_comp = true;
          break;
        }
      }
    }

    if (!has_comp) {
      // no error, we just don't need to keep going
      return true;
    }
  }

  ECS_COMPONENT(ecs, TbThirdPersonMovementComponent);

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
  ecs_set_ptr(ecs, e, TbThirdPersonMovementComponent, &comp);

  return true;
}

bool try_attach_tp_cam_comp(ecs_world_t *ecs, ecs_entity_t e,
                            json_object *json) {
  {
    bool has_comp = false;

    json_object_object_foreach(json, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, ThirdPersonCameraComponentIdStr) == 0) {
          has_comp = true;
          break;
        }
      }
    }

    if (!has_comp) {
      // no error, we just don't need to keep going
      return true;
    }
  }

  ECS_TAG(ecs, TbThirdPersonCameraComponent);
  ecs_add(ecs, e, TbThirdPersonCameraComponent);

  return true;
}

// Third person movement component wants to construct direction vectors to
// the camera that is relevant to the control rig. We also want
// to know what body we want to exert force on
// Need the hierarchy to be constructed first so we look it up in post-load
// Expecting the camera to be attached to some child entity
void post_load_tp_movement(ecs_world_t *ecs, ecs_entity_t e) {
  ECS_COMPONENT(ecs, TbThirdPersonMovementComponent);
  ECS_TAG(ecs, TbThirdPersonCameraComponent);
  ECS_COMPONENT(ecs, TbTransformComponent);
  ECS_COMPONENT(ecs, TbCameraComponent);
  ECS_COMPONENT(ecs, TbRigidbodyComponent);
  TbThirdPersonMovementComponent *movement =
      ecs_get_mut(ecs, e, TbThirdPersonMovementComponent);
  const TbTransformComponent *trans = ecs_get(ecs, e, TbTransformComponent);

  TB_CHECK(movement->body == 0, "Didn't expect body to already be set");
  TB_CHECK(movement->camera == 0, "Didn't expect camera to already be set");
  TB_CHECK(trans->parent != TbInvalidEntityId, "Expected a valid parent");
  {
    bool sibling_camera = false;
    const TbTransformComponent *parent = tb_transform_get_parent(ecs, trans);
    for (uint32_t i = 0; i < parent->child_count; ++i) {
      ecs_entity_t sibling = parent->children[i];
      if (ecs_has(ecs, sibling, TbThirdPersonCameraComponent) &&
          ecs_has(ecs, sibling, TbCameraComponent)) {
        movement->camera = sibling;
        sibling_camera = true;
        break;
      }
    }
    TB_CHECK(sibling_camera, "Didn't find sibling that has camera");
  }
  {
    bool parent_body = ecs_has(ecs, trans->parent, TbRigidbodyComponent);
    if (parent_body) {
      movement->body = trans->parent;
    }
    TB_CHECK(parent_body, "Didn't find parent that has rigidbody");
  }
}

bool create_third_person_components(ecs_world_t *ecs, ecs_entity_t e,
                                    const char *source_path,
                                    const cgltf_node *node,
                                    json_object *extra) {
  (void)source_path;
  (void)extra;

  ECS_COMPONENT(ecs, TbThirdPersonMovementComponent);
  ECS_TAG(ecs, TbThirdPersonCameraComponent);

  bool ret = true;
  if (node && extra) {
    ret &= try_attach_tp_move_comp(ecs, e, extra);
    ret &= try_attach_tp_cam_comp(ecs, e, extra);
  }
  return ret;
}

void post_load_third_person_components(ecs_world_t *ecs, ecs_entity_t e) {
  ECS_COMPONENT(ecs, TbThirdPersonMovementComponent);

  if (ecs_has(ecs, e, TbThirdPersonMovementComponent)) {
    post_load_tp_movement(ecs, e);
  }
}

void remove_third_person_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TbThirdPersonMovementComponent);
  ECS_TAG(ecs, TbThirdPersonCameraComponent);

  // Remove movement from entities
  {
    ecs_filter_t *filter = ecs_filter(
        ecs, {
                 .terms =
                     {
                         {.id = ecs_id(TbThirdPersonMovementComponent)},
                     },
             });

    ecs_iter_t it = ecs_filter_iter(ecs, filter);
    while (ecs_filter_next(&it)) {
      TbThirdPersonMovementComponent *comps =
          ecs_field(&it, TbThirdPersonMovementComponent, 1);

      for (int32_t i = 0; i < it.count; ++i) {
        comps[i] = (TbThirdPersonMovementComponent){0};
      }
    }

    ecs_filter_fini(filter);
  }
}

void tb_register_third_person_components(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbAssetSystem);
  ECS_TAG(ecs, ThirdPersonAssetSystem);

  TbAssetSystem asset = {
      .add_fn = create_third_person_components,
      .post_load_fn = post_load_third_person_components,
      .rem_fn = remove_third_person_components,
  };

  ecs_set_ptr(ecs, ecs_id(ThirdPersonAssetSystem), TbAssetSystem, &asset);
}
