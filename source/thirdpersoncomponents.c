#include "thirdpersoncomponents.h"

#include "assetsystem.h"
#include "cameracomponent.h"
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

  ECS_COMPONENT(ecs, ThirdPersonMovementComponent);

  ThirdPersonMovementComponent comp = {0};
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "speed") == 0) {
      comp.speed = (float)json_object_get_double(value);
    }
  }
  ecs_set_ptr(ecs, e, ThirdPersonMovementComponent, &comp);

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

  ECS_COMPONENT(ecs, ThirdPersonCameraComponent);

  ThirdPersonCameraComponent comp = {0};
  // TODO: Parse parameters
  ecs_set_ptr(ecs, e, ThirdPersonCameraComponent, &comp);

  return true;
}

// Third person movement component wants to construct direction vectors to
// the camera that is relevant to the control rig.
// Need the hierarchy to be constructed first so we look it up in post-load
// Expecting the camera to be attached to some child entity
void post_load_tp_movement(ecs_world_t *ecs, ecs_entity_t e) {
  ECS_COMPONENT(ecs, ThirdPersonMovementComponent);
  ECS_COMPONENT(ecs, ThirdPersonCameraComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, CameraComponent);
  ThirdPersonMovementComponent *movement =
      ecs_get_mut(ecs, e, ThirdPersonMovementComponent);
  const TransformComponent *trans = ecs_get(ecs, e, TransformComponent);

  TB_CHECK(movement->camera == 0, "Didn't expect camera to already be set");
  TB_CHECK(trans->child_count > 0,
           "Expected third person movement to have children");
  bool has_camera = false;
  for (uint32_t i = 0; i < trans->child_count; ++i) {
    ecs_entity_t child = trans->children[i];
    if (ecs_has(ecs, child, ThirdPersonCameraComponent) &&
        ecs_has(ecs, child, CameraComponent)) {
      movement->camera = child;
      has_camera = true;
      break;
    }
  }
  TB_CHECK(has_camera, "Didn't find child that has camera");
}

bool create_third_person_components(ecs_world_t *ecs, ecs_entity_t e,
                                    const char *source_path,
                                    const cgltf_node *node,
                                    json_object *extra) {
  (void)source_path;
  (void)extra;

  ECS_COMPONENT(ecs, ThirdPersonMovementComponent);
  ECS_COMPONENT(ecs, ThirdPersonCameraComponent);

  bool ret = true;
  if (node && extra) {
    ret &= try_attach_tp_move_comp(ecs, e, extra);
    ret &= try_attach_tp_cam_comp(ecs, e, extra);
  }
  return ret;
}

void post_load_third_person_components(ecs_world_t *ecs, ecs_entity_t e) {
  ECS_COMPONENT(ecs, ThirdPersonMovementComponent);

  if (ecs_has(ecs, e, ThirdPersonMovementComponent)) {
    post_load_tp_movement(ecs, e);
  }
}

void remove_third_person_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ThirdPersonMovementComponent);
  ECS_COMPONENT(ecs, ThirdPersonCameraComponent);

  // Remove movement from entities
  {
    ecs_filter_t *filter = ecs_filter(
        ecs, {
                 .terms =
                     {
                         {.id = ecs_id(ThirdPersonMovementComponent)},
                     },
             });

    ecs_iter_t it = ecs_filter_iter(ecs, filter);
    while (ecs_filter_next(&it)) {
      ThirdPersonMovementComponent *comps =
          ecs_field(&it, ThirdPersonMovementComponent, 1);

      for (int32_t i = 0; i < it.count; ++i) {
        comps[i] = (ThirdPersonMovementComponent){0};
      }
    }

    ecs_filter_fini(filter);
  }

  // Remove camera from entities
  {
    ecs_filter_t *filter =
        ecs_filter(ecs, {
                            .terms =
                                {
                                    {.id = ecs_id(ThirdPersonCameraComponent)},
                                },
                        });

    ecs_iter_t it = ecs_filter_iter(ecs, filter);
    while (ecs_filter_next(&it)) {
      ThirdPersonCameraComponent *comps =
          ecs_field(&it, ThirdPersonCameraComponent, 1);

      for (int32_t i = 0; i < it.count; ++i) {
        comps[i] = (ThirdPersonCameraComponent){0};
      }
    }

    ecs_filter_fini(filter);
  }
}

// A dummy type whose singleton entity we use to house our asset system
typedef struct ThirdPersonAssetSystem {
  float dummy;
} ThirdPersonAssetSystem;

void tb_register_third_person_components(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, ThirdPersonAssetSystem);

  AssetSystem asset = {
      .add_fn = create_third_person_components,
      .post_load_fn = post_load_third_person_components,
      .rem_fn = remove_third_person_components,
  };

  ecs_set_ptr(ecs, ecs_id(ThirdPersonAssetSystem), AssetSystem, &asset);
}
