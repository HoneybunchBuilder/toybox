#include "cameracomponent.h"

#include "assetsystem.h"
#include "camerasystem.h"
#include "common.hlsli"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

bool create_camera_component(ecs_world_t *ecs, ecs_entity_t e,
                             const char *source_path, const cgltf_node *node,
                             json_object *extra) {
  (void)source_path;
  (void)extra;

  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, CameraComponent);

  bool ret = true;
  if (node->camera) {
    ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);

    if (node->camera->type == cgltf_camera_type_perspective) {
      ecs_singleton_modified(ecs, ViewSystem);
      cgltf_camera_perspective *persp = &node->camera->data.perspective;
      CameraComponent comp = {
          .view_id = tb_view_system_create_view(view_sys),
          .aspect_ratio = persp->aspect_ratio,
          .fov = persp->yfov,
          .near = persp->znear,
          .far = persp->zfar,
      };
      ecs_set_ptr(ecs, e, CameraComponent, &comp);
    } else {
      // TODO: Handle ortho camera / invalid camera
      TB_CHECK(false, "Orthographic camera unsupported");
      ret = false;
    }
  }
  return ret;
}

void destroy_camera_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, CameraComponent);

  // Remove camera component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(CameraComponent)},
                              },
                      });

  ecs_iter_t cam_it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&cam_it)) {
    CameraComponent *cam = ecs_field(&cam_it, CameraComponent, 1);

    for (int32_t i = 0; i < cam_it.count; ++i) {
      *cam = (CameraComponent){0};
    }
  }
  ecs_filter_fini(filter);
}

void tb_register_camera_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, CameraSystem);

  // Add an asset system to handle loading cameras
  AssetSystem asset = {
      .add_fn = create_camera_component,
      .rem_fn = destroy_camera_components,
  };
  ecs_set_ptr(ecs, ecs_id(CameraSystem), AssetSystem, &asset);
}
