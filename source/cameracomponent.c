#include "cameracomponent.h"

#include "camerasystem.h"
#include "common.hlsli"
#include "rendersystem.h"
#include "renderthread.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

ECS_COMPONENT_DECLARE(TbCameraComponent);

bool tb_load_camera_comp(TbWorld *world, ecs_entity_t ent,
                         const char *source_path, const cgltf_node *node,
                         json_object *json) {
  (void)source_path;
  (void)json;

  tb_auto ecs = world->ecs;

  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

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
    ecs_set_ptr(ecs, ent, TbCameraComponent, &comp);
  } else {
    // TODO: Handle ortho camera / invalid camera
    TB_CHECK(false, "Orthographic camera unsupported");
    return false;
  }

  return true;
}

ecs_entity_t tb_register_camera_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbCameraComponent);

  // Metadata for camera component
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

  // Returning 0 means we need no custom descriptor for editor UI
  return 0;
}

TB_REGISTER_COMP(tb, camera)
