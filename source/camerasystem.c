#include "camerasystem.h"

#include "cameracomponent.h"
#include "common.hlsli"
#include "profiling.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

bool create_camera_system(CameraSystem *self,
                          const CameraSystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  // Find the necessary systems
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  TB_CHECK_RETURN(view_system,
                  "Failed to find view system which cameras depend on", false);

  *self = (CameraSystem){
      .view_system = view_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };
  return true;
}

void destroy_camera_system(CameraSystem *self) { *self = (CameraSystem){0}; }

void tick_camera_system_internal(CameraSystem *self, const SystemInput *input,
                                 SystemOutput *output, float delta_seconds) {
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "Camera System Tick", TracyCategoryColorCore, true);

  const uint32_t camera_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *cameras =
      tb_get_column_check_id(input, 0, 0, CameraComponentId);
  const PackedComponentStore *transforms =
      tb_get_column_check_id(input, 0, 1, TransformComponentId);

  if (camera_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  ViewSystem *view_system = self->view_system;

  for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
    const CameraComponent *cam_comp =
        tb_get_component(cameras, cam_idx, CameraComponent);
    const TransformComponent *trans_comp =
        tb_get_component(transforms, cam_idx, TransformComponent);

    // Eval transform heirarchy
    CommonViewData view_data = {
        .view_pos = trans_comp->transform.position,
    };

    const float3 forward = transform_get_forward(&trans_comp->transform);
    float4x4 view =
        look_forward(trans_comp->transform.position, forward, TB_UP);

    float4x4 proj = perspective(cam_comp->fov, cam_comp->aspect_ratio,
                                cam_comp->near, cam_comp->far);

    view_data.v = view;
    view_data.p = proj;
    view_data.inv_proj = inv_mf44(proj);
    view_data.proj_params = (float4){cam_comp->near, cam_comp->far,
                                     cam_comp->aspect_ratio, cam_comp->fov};

    // Calculate view projection matrix
    view_data.vp = mulmf44(proj, view);

    // Inverse
    view_data.inv_vp = inv_mf44(view_data.vp);

    Frustum frustum = frustum_from_view_proj(&view_data.vp);

    // HACK - setting target here to the swapchain in a janky way that's
    // just used to facilitate other hacks
    // The render pipeline will use whatever view targets the swapchain
    // to do SSAO
    tb_view_system_set_view_target(
        view_system, cam_comp->view_id,
        self->view_system->render_target_system->swapchain);
    tb_view_system_set_view_data(view_system, cam_comp->view_id, &view_data);
    tb_view_system_set_view_frustum(view_system, cam_comp->view_id, &frustum);
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(camera, CameraSystem, CameraSystemDescriptor)

void tick_camera_system(void *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick Camera System");
  tick_camera_system_internal((CameraSystem *)self, input, output,
                              delta_seconds);
}

void tb_camera_system_descriptor(SystemDescriptor *desc,
                                 const CameraSystemDescriptor *camera_desc) {
  *desc = (SystemDescriptor){
      .name = "Camera",
      .size = sizeof(CameraSystem),
      .id = CameraSystemId,
      .desc = (InternalDescriptor)camera_desc,
      .system_dep_count = 1,
      .system_deps[0] = ViewSystemId,
      .create = tb_create_camera_system,
      .destroy = tb_destroy_camera_system,
      .tick_fn_count = 1,
      .tick_fns =
          {
              {
                  .dep_count = 1,
                  .deps[0] = {2, {CameraComponentId, TransformComponentId}},
                  .system_id = CameraSystemId,
                  .order = E_TICK_PRE_PHYSICS,
                  .function = tick_camera_system,
              },
          },
  };
}

void flecs_tick_camera(ecs_iter_t *it) {
  CameraSystem *sys = ecs_field(it, CameraSystem, 1);
  CameraComponent *camera = ecs_field(it, CameraComponent, 2);
  TransformComponent *transform = ecs_field(it, TransformComponent, 3);

  // Eval transform heirarchy
  CommonViewData view_data = {
      .view_pos = transform->transform.position,
  };

  const float3 forward = transform_get_forward(&transform->transform);
  float4x4 view = look_forward(transform->transform.position, forward, TB_UP);

  float4x4 proj =
      perspective(camera->fov, camera->aspect_ratio, camera->near, camera->far);

  view_data.v = view;
  view_data.p = proj;
  view_data.inv_proj = inv_mf44(proj);
  view_data.proj_params =
      (float4){camera->near, camera->far, camera->aspect_ratio, camera->fov};

  // Calculate view projection matrix
  view_data.vp = mulmf44(proj, view);

  // Inverse
  view_data.inv_vp = inv_mf44(view_data.vp);

  Frustum frustum = frustum_from_view_proj(&view_data.vp);

  // HACK - setting target here to the swapchain in a janky way that's
  // just used to facilitate other hacks
  // The render pipeline will use whatever view targets the swapchain
  // to do SSAO
  tb_view_system_set_view_target(
      sys->view_system, camera->view_id,
      sys->view_system->render_target_system->swapchain);
  tb_view_system_set_view_data(sys->view_system, camera->view_id, &view_data);
  tb_view_system_set_view_frustum(sys->view_system, camera->view_id, &frustum);
}

void tb_register_camera_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, CameraComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, CameraSystem);
  ECS_COMPONENT(ecs, ViewSystem);

  ecs_singleton_set(ecs, CameraSystem,
                    {
                        .view_system = ecs_singleton_get_mut(ecs, ViewSystem),
                        .tmp_alloc = tmp_alloc,
                        .std_alloc = std_alloc,
                    });
  ECS_SYSTEM(ecs, flecs_tick_camera, EcsOnUpdate, CameraSystem(CameraSystem),
             CameraComponent, TransformComponent);
}
