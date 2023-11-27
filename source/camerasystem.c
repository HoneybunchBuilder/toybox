#include "camerasystem.h"

#include "assetsystem.h"
#include "cameracomponent.h"
#include "common.hlsli"
#include "profiling.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

void camera_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Camera Update System", TracyCategoryColorCore, true);

  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, ViewSystem);

  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  ecs_singleton_modified(ecs, ViewSystem);

  CameraComponent *camera = ecs_field(it, CameraComponent, 1);
  TransformComponent *transform = ecs_field(it, TransformComponent, 2);

  float4x4 cam_world = tb_transform_get_world_matrix(ecs, transform);

  float3 pos = cam_world.col3.xyz;
  float3 forward = mulf33(m44tom33(cam_world), TB_FORWARD);

  // Eval transform heirarchy
  CommonViewData view_data = {.view_pos = pos};

  float4x4 view = look_forward(pos, forward, TB_UP);

  float4x4 proj =
      perspective(camera->fov, camera->aspect_ratio, camera->near, camera->far);

  view_data.v = view;
  view_data.p = proj;
  view_data.inv_proj = inv_mf44(proj);
  view_data.proj_params =
      (float4){camera->near, camera->far, camera->aspect_ratio, camera->fov};

  // TODO: Find a cleaner expression for updating this
  camera->width = view_sys->render_system->render_thread->swapchain.width;
  camera->height = view_sys->render_system->render_thread->swapchain.height;

  // Calculate view projection matrix
  view_data.vp = mulmf44(proj, view);

  // Inverse
  view_data.inv_vp = inv_mf44(view_data.vp);

  Frustum frustum = frustum_from_view_proj(&view_data.vp);

  // HACK - setting target here to the swapchain in a janky way that's
  // just used to facilitate other hacks
  tb_view_system_set_view_target(view_sys, camera->view_id,
                                 view_sys->render_target_system->swapchain);
  tb_view_system_set_view_data(view_sys, camera->view_id, &view_data);
  tb_view_system_set_view_frustum(view_sys, camera->view_id, &frustum);
  TracyCZoneEnd(ctx);
}

void tb_register_camera_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                            TbAllocator tmp_alloc) {
  ECS_COMPONENT(ecs, CameraComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, CameraSystem);
  ECS_COMPONENT(ecs, AssetSystem);

  ecs_singleton_set(ecs, CameraSystem,
                    {
                        .tmp_alloc = tmp_alloc,
                        .std_alloc = std_alloc,
                    });
  ECS_SYSTEM(ecs, camera_update_tick, EcsOnUpdate, CameraComponent,
             TransformComponent);

  tb_register_camera_component(ecs);
}

void tb_unregister_camera_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, CameraSystem);
  CameraSystem *sys = ecs_singleton_get_mut(ecs, CameraSystem);
  *sys = (CameraSystem){0};
  ecs_singleton_remove(ecs, CameraSystem);
}
