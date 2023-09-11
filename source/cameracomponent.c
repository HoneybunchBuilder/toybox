#include "cameracomponent.h"

#include "common.hlsli"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

CameraComponent tb_create_camera_component2(ecs_world_t *ecs,
                                            cgltf_camera *desc) {
  ECS_COMPONENT(ecs, ViewSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  if (desc->type == cgltf_camera_type_perspective) {
    ecs_singleton_modified(ecs, ViewSystem);
    cgltf_camera_perspective *persp = &desc->data.perspective;
    return (CameraComponent){
        .view_id = tb_view_system_create_view(view_sys),
        .aspect_ratio = persp->aspect_ratio,
        .fov = persp->yfov,
        .near = persp->znear,
        .far = persp->zfar,
    };
  } else {
    // TODO: Handle ortho camera / invalid camera
    TB_CHECK(false, "Orthographic camera unsupported");
    return (CameraComponent){0};
  }
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
