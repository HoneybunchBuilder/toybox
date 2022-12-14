#include "cameracomponent.h"

#include "common.hlsli"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

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
      .near = 0.1f,
      .far = 500.0f,
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
