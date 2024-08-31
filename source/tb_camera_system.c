#include "tb_camera_system.h"

#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_common.slangh"
#include "tb_profiling.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_transform_component.h"
#include "tb_view_system.h"
#include "tb_world.h"

#include <flecs.h>

void tb_register_camera_sys(TbWorld *world);
void tb_unregister_camera_sys(TbWorld *world);

TB_REGISTER_SYS(tb, camera, TB_CAMERA_SYS_PRIO)

void camera_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Camera Update System", TracyCategoryColorCore, true);

  tb_auto ecs = it->world;

  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  ecs_singleton_modified(ecs, TbViewSystem);

  tb_auto cameras = ecs_field(it, TbCameraComponent, 1);

  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto entity = it->entities[i];
    tb_auto camera = &cameras[i];

    tb_auto cam_world = tb_transform_get_world_matrix(ecs, entity);

    float3 pos = cam_world.col3.xyz;
    float3 forward = tb_mulf33f3(tb_f44tof33(cam_world), TB_FORWARD);

    // Eval transform heirarchy
    TbViewData view_data = {.view_pos = pos};

    tb_auto view = tb_look_forward(pos, forward, TB_UP);

    tb_auto proj = tb_perspective(camera->fov, camera->aspect_ratio,
                                  camera->near, camera->far);

    view_data.v = view;
    view_data.p = proj;
    view_data.inv_proj = tb_invf44(proj);
    view_data.proj_params =
        (float4){camera->near, camera->far, camera->aspect_ratio, camera->fov};

    // TODO: Find a cleaner expression for updating this
    camera->width = (float)view_sys->rnd_sys->render_thread->swapchain.width;
    camera->height = (float)view_sys->rnd_sys->render_thread->swapchain.height;

    // Calculate view projection matrix
    view_data.vp = tb_mulf44f44(proj, view);

    // Inverse
    view_data.inv_vp = tb_invf44(view_data.vp);

    TbFrustum frustum = tb_frustum_from_view_proj(&view_data.vp);

    // HACK - setting target here to the swapchain in a janky way that's
    // just used to facilitate other hacks
    tb_view_system_set_view_target(view_sys, camera->view_id,
                                   view_sys->rt_sys->swapchain);
    tb_view_system_set_view_data(view_sys, camera->view_id, &view_data);
    tb_view_system_set_view_frustum(view_sys, camera->view_id, &frustum);
  }
  TracyCZoneEnd(ctx);
}

void tb_register_camera_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Camera Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_SYSTEM(ecs, camera_update_tick, EcsPostUpdate, TbCameraComponent,
             TbTransformComponent);

  TracyCZoneEnd(ctx);
}

void tb_unregister_camera_sys(TbWorld *world) { (void)world; }
