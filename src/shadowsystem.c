#include "shadowsystem.h"

#include "lightcomponent.h"
#include "profiling.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

bool create_shadow_system(ShadowSystem *self,
                          const ShadowSystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  // Find necessary systems
  ViewSystem *view_system = (ViewSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, ViewSystemId);
  TB_CHECK_RETURN(view_system,
                  "Failed to find view system which shadows depend on", false);

  *self = (ShadowSystem){
      .view_system = view_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };
  return true;
}

void destroy_shadow_system(ShadowSystem *self) { *self = (ShadowSystem){0}; }

void tick_shadow_system(ShadowSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "Shadow System Tick", TracyCategoryColorRendering, true);

  const uint32_t light_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *dir_lights =
      tb_get_column_check_id(input, 0, 0, DirectionalLightComponentId);
  const PackedComponentStore *transforms =
      tb_get_column_check_id(input, 0, 1, TransformComponentId);

  if (light_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  ViewSystem *view_system = self->view_system;

  for (uint32_t light_idx = 0; light_idx < light_count; ++light_idx) {
    const DirectionalLightComponent *dir_light =
        tb_get_component(dir_lights, light_idx, DirectionalLightComponent);
    const TransformComponent *trans_comp =
        tb_get_component(transforms, light_idx, TransformComponent);

    Transform transform = trans_comp->transform;

    CommonViewData data = {
        .view_pos = transform.position,
    };

    float4x4 model = {.row0 = {0}};
    transform_to_matrix(&model, &transform);
    const float3 forward = f4tof3(model.row2);

    // Readjust transform position based on the forward direction
    // so that we can pretend to project from an infinite distance
    const float distance = 100.0f;
    const float3 displacement = forward * (-distance / 2.0f);

    float4x4 view = {.row0 = {0}};
    look_at(&view, displacement, (float3){0}, (float3){0, 1, 0});

    float4x4 proj = {.row0 = {0}};
    orthographic(&proj, distance / 2.0f, distance / 2.0f, 0.0f, distance);

    // Calculate view projection matrix
    mulmf44(&proj, &view, &data.vp);

    // Inverse
    data.inv_vp = inv_mf44(data.vp);

    Frustum frustum = frustum_from_view_proj(&data.vp);

    tb_view_system_set_view_data(view_system, dir_light->view, &data);
    tb_view_system_set_view_frustum(view_system, dir_light->view, &frustum);
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(shadow, ShadowSystem, ShadowSystemDescriptor)

void tb_shadow_system_descriptor(SystemDescriptor *desc,
                                 const ShadowSystemDescriptor *shadow_desc) {
  *desc = (SystemDescriptor){
      .name = "Shadow",
      .size = sizeof(ShadowSystem),
      .id = ShadowSystemId,
      .desc = (InternalDescriptor)shadow_desc,
      .dep_count = 1,
      .deps[0] =
          {
              .count = 2,
              .dependent_ids = {DirectionalLightComponentId,
                                TransformComponentId},
          },
      .system_dep_count = 1,
      .system_deps[0] = ViewSystemId,
      .create = tb_create_shadow_system,
      .destroy = tb_destroy_shadow_system,
      .tick = tb_tick_shadow_system,
  };
}
