#include "camerasystem.h"

#include "cameracomponent.h"
#include "common.hlsli"
#include "profiling.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

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

void tick_camera_system(CameraSystem *self, const SystemInput *input,
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

    // TODO: Eval transform heirarchy
    Transform transform = trans_comp->transform;

    CommonViewData data = {
        .view_pos = transform.position,
    };

    float4x4 model = {.row0 = {0}};
    transform_to_matrix(&model, &transform);
    const float3 forward = f4tof3(model.row2);
    const float3 up = f4tof3(model.row1);

    float4x4 view = {.row0 = {0}};
    look_forward(&view, transform.position, forward, up);

    float4x4 proj = {.row0 = {0}};
    perspective(&proj, cam_comp->fov, cam_comp->aspect_ratio, cam_comp->near,
                cam_comp->far);

    // Calculate view projection matrix
    mulmf44(&proj, &view, &data.vp);

    // DEBUG
    {
      float3 f = {0, -1, 0};
      float3 u = {1, 0, 0};
      float4x4 tmp_v = {.row0 = {0}};
      look_forward(&tmp_v, (float3){0}, f, u);

      float fov = tb_deg_to_rad(90.0f);

      float4x4 tmp_p = {.row0 = {0}};
      perspective(&tmp_p, fov, 1.0f, 0.1f, 512.0f);

      float4x4 tmp_vp;
      mulmf44(&tmp_p, &tmp_v, &tmp_vp);
      // SDL_TriggerBreakpoint();
    }

    // Inverse
    data.inv_vp = inv_mf44(data.vp);

    Frustum frustum = frustum_from_view_proj(&data.vp);

    tb_view_system_set_view_data(view_system, cam_comp->view_id, &data);
    tb_view_system_set_view_frustum(view_system, cam_comp->view_id, &frustum);
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(camera, CameraSystem, CameraSystemDescriptor)

void tb_camera_system_descriptor(SystemDescriptor *desc,
                                 const CameraSystemDescriptor *camera_desc) {
  *desc = (SystemDescriptor){
      .name = "Camera",
      .size = sizeof(CameraSystem),
      .id = CameraSystemId,
      .desc = (InternalDescriptor)camera_desc,
      .dep_count = 1,
      .deps[0] = {2, {CameraComponentId, TransformComponentId}},
      .system_dep_count = 1,
      .system_deps[0] = ViewSystemId,
      .create = tb_create_camera_system,
      .destroy = tb_destroy_camera_system,
      .tick = tb_tick_camera_system,
  };
}
