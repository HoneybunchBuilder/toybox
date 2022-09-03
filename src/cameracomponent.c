#include "cameracomponent.h"

#include "tbgltf.h"

#include "world.h"

bool create_camera_component(CameraComponent *comp,
                             const cgltf_camera_perspective *desc,
                             uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (CameraComponent){
      .aspect_ratio = desc->aspect_ratio,
      .fov = desc->yfov,
      .near = desc->znear,
      .far = desc->zfar,
  };
  return true;
}

void destroy_camera_component(CameraComponent *comp) {
  *comp = (CameraComponent){0};
}

TB_DEFINE_COMPONENT(camera, CameraComponent, cgltf_camera_perspective)

void tb_camera_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Camera";
  desc->size = sizeof(CameraComponent);
  desc->id = CameraComponentId;
  desc->create = tb_create_camera_component;
  desc->destroy = tb_destroy_camera_component;
}
