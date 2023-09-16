#include "cameracomponent.h"

#include "common.hlsli"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

bool tb_create_camera_component2(ecs_world_t *ecs, ecs_entity_t e,
                                 const char *source_path, cgltf_node *node,
                                 json_object *extra) {
  (void)extra;
  bool ret = true;
  if (node->camera) {
    ECS_COMPONENT(ecs, ViewSystem);
    ECS_COMPONENT(ecs, CameraComponent);

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

void tb_destroy_camera_component2(ecs_world_t *ecs) {
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

bool create_camera_component(CameraComponent *comp,
                             const cgltf_camera_perspective *desc,
                             uint32_t system_dep_count,
                             System *const *system_deps) {
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);

  *comp = (CameraComponent){
      .view_id = tb_view_system_create_view(view_system),
      .aspect_ratio = desc->aspect_ratio,
      .fov = desc->yfov,
      .near = desc->znear,
      .far = desc->zfar,
  };
  return true;
}

void destroy_camera_component(CameraComponent *comp, uint32_t system_dep_count,
                              System *const *system_deps) {
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  (void)view_system;
  *comp = (CameraComponent){0};
}

TB_DEFINE_COMPONENT(camera, CameraComponent, cgltf_camera_perspective)

void tb_camera_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Camera",
      .size = sizeof(CameraComponent),
      .id = CameraComponentId,
      .create = tb_create_camera_component,
      .destroy = tb_destroy_camera_component,
      .system_dep_count = 1,
      .system_deps[0] = ViewSystemId,
  };
}
