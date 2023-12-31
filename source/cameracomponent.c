#include "cameracomponent.h"

#include "assetsystem.h"
#include "camerasystem.h"
#include "common.hlsli"
#include "rendersystem.h"
#include "renderthread.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

ECS_COMPONENT_DECLARE(TbCameraComponent);

bool create_camera_component(ecs_world_t *ecs, ecs_entity_t e,
                             const char *source_path, const cgltf_node *node,
                             json_object *extra) {
  (void)source_path;
  (void)extra;

  ECS_COMPONENT(ecs, TbViewSystem);

  bool ret = true;
  if (node->camera) {
    TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

    if (node->camera->type == cgltf_camera_type_perspective) {
      ecs_singleton_modified(ecs, TbViewSystem);
      cgltf_camera_perspective *persp = &node->camera->data.perspective;
      TbCameraComponent comp = {
          .view_id = tb_view_system_create_view(view_sys),
          .aspect_ratio = persp->aspect_ratio,
          .fov = persp->yfov,
          .near = persp->znear,
          .far = persp->zfar,
          .width = (float)view_sys->rnd_sys->render_thread->swapchain.width,
          .height = (float)view_sys->rnd_sys->render_thread->swapchain.height,
      };
      ecs_set_ptr(ecs, e, TbCameraComponent, &comp);
    } else {
      // TODO: Handle ortho camera / invalid camera
      TB_CHECK(false, "Orthographic camera unsupported");
      ret = false;
    }
  }
  return ret;
}

void destroy_camera_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TbViewSystem);

  // Remove camera component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(TbCameraComponent)},
                              },
                      });

  ecs_iter_t cam_it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&cam_it)) {
    TbCameraComponent *cam = ecs_field(&cam_it, TbCameraComponent, 1);

    for (int32_t i = 0; i < cam_it.count; ++i) {
      *cam = (TbCameraComponent){0};
    }
  }
  ecs_filter_fini(filter);
}

void tb_register_camera_component(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbAssetSystem);
  ECS_COMPONENT_DEFINE(ecs, TbCameraComponent);
  ECS_TAG(ecs, TbCameraSystem);

  // Metadata for transform component
  {
    ecs_struct(ecs,
               {
                   .entity = ecs_id(TbCameraComponent),
                   .members =
                       {
                           {.name = "view_id", .type = ecs_id(ecs_i32_t)},
                           {.name = "aspect_ratio", .type = ecs_id(ecs_f32_t)},
                           {.name = "fov", .type = ecs_id(ecs_f32_t)},
                           {.name = "near", .type = ecs_id(ecs_f32_t)},
                           {.name = "far", .type = ecs_id(ecs_f32_t)},
                           {.name = "width", .type = ecs_id(ecs_f32_t)},
                           {.name = "height", .type = ecs_id(ecs_f32_t)},
                       },
               });
  }

  // Add an asset system to handle loading cameras
  TbAssetSystem asset = {
      .add_fn = create_camera_component,
      .rem_fn = destroy_camera_components,
  };
  ecs_set_ptr(ecs, ecs_id(TbCameraSystem), TbAssetSystem, &asset);
}
