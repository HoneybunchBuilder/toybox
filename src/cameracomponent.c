#include "cameracomponent.h"

#include "tbgltf.h"

#include "world.h"

void create_camera_component(CameraComponent *comp,
                             const cgltf_camera_perspective *desc) {
  *comp = (CameraComponent){
      .aspect_ratio = desc->aspect_ratio,
      .fov = desc->yfov,
      .near = desc->znear,
      .far = desc->zfar,
  };
}

void destroy_camera_component(CameraComponent *comp) {
  *comp = (CameraComponent){0};
}

bool tb_create_camera_component(void *comp, InternalDescriptor desc) {
  create_camera_component((CameraComponent *)comp,
                          (const cgltf_camera_perspective *)desc);
  return true;
}

void tb_destroy_camera_component(void *comp) {
  destroy_camera_component((CameraComponent *)comp);
}

void tb_camera_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Camera";
  desc->size = sizeof(CameraComponent);
  desc->id = CameraComponentId;
  desc->create = tb_create_camera_component;
  desc->destroy = tb_destroy_camera_component;
}
