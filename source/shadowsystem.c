#include "shadowsystem.h"

#include "cameracomponent.h"
#include "lightcomponent.h"
#include "profiling.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"
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
  VisualLoggingSystem *vlog =
      (VisualLoggingSystem *)tb_find_system_dep_self_by_id(
          system_deps, system_dep_count, VisualLoggingSystemId);
  TB_CHECK_RETURN(
      vlog, "Failed to find visual logging system which shadows depend on",
      false);

  *self = (ShadowSystem){
      .view_system = view_system,
      .vlog = vlog,
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

  const EntityId *light_entities = tb_get_column_entity_ids(input, 0);
  const uint32_t light_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *dir_lights =
      tb_get_column_check_id(input, 0, 0, DirectionalLightComponentId);
  const PackedComponentStore *light_transforms =
      tb_get_column_check_id(input, 0, 1, TransformComponentId);

  const uint32_t camera_count = tb_get_column_component_count(input, 1);
  const PackedComponentStore *camera_components =
      tb_get_column_check_id(input, 1, 0, CameraComponentId);

  if (light_count == 0 || camera_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Copy directional lights for output; we want to write the cacscade splits
  DirectionalLightComponent *out_lights =
      tb_alloc_nm_tp(self->tmp_alloc, light_count, DirectionalLightComponent);
  {
    const DirectionalLightComponent *in_lights =
        tb_get_component(dir_lights, 0, DirectionalLightComponent);
    SDL_memcpy(out_lights, in_lights,
               light_count * sizeof(DirectionalLightComponent));
  }

  ViewSystem *view_system = self->view_system;

  // We want to fit the orthographic shadow projection matrix to the area
  // visible
  // Assume just one view for now
  const CameraComponent *camera_component =
      tb_get_component(camera_components, 0, CameraComponent);

  const float near = camera_component->near;
  const float far = camera_component->far;

  // Get inverse camera vp matrix from view system
  float4x4 inv_cam_vp = {.row0 = {0}};
  {
    const View *v = tb_get_view(self->view_system, camera_component->view_id);
    inv_cam_vp = v->view_data.inv_vp;
  }

  const float cascade_split_lambda = 0.95f;
  float cascade_splits[TB_CASCADE_COUNT] = {0};

  float clip_range = far - near;

  float min_z = near;
  float max_z = near + clip_range;

  float range = max_z - min_z;
  float ratio = max_z / min_z;

  // Calculate split depths based on view camera frustum
  // Based on method presented in
  // https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
  for (uint32_t i = 0; i < TB_CASCADE_COUNT; i++) {
    float p = (i + 1) / (float)TB_CASCADE_COUNT;
    float log = min_z * SDL_powf(ratio, p);
    float uniform = min_z + range * p;
    float d = cascade_split_lambda * (log - uniform) + uniform;
    cascade_splits[i] = (d - near) / clip_range;
  }

  for (uint32_t light_idx = 0; light_idx < light_count; ++light_idx) {
    const DirectionalLightComponent *dir_light =
        tb_get_component(dir_lights, light_idx, DirectionalLightComponent);
    const TransformComponent *trans_comp =
        tb_get_component(light_transforms, light_idx, TransformComponent);

    Transform transform = trans_comp->transform;

    CommonViewData data = {
        .view_pos = transform.position,
    };

    float last_split_dist = 0.0f;
    for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
         ++cascade_idx) {
      float split_dist = cascade_splits[cascade_idx];

      float3 frustum_corners[8] = {
#ifdef TB_USE_INVERSE_DEPTH
          // Inverse near and far because main camera uses reverse depth
          {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, // Near
          {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},
          {-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, // Far
          {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
#else
          {-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, // Near
          {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
          {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, // Far
          {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},
#endif
      };

      // Project into world space
      for (uint32_t i = 0; i < 8; ++i) {
        const float3 corner = frustum_corners[i];
        float4 inv_corner = mul4f44f(
            (float4){corner[0], corner[1], corner[2], 1.0f}, inv_cam_vp);
        frustum_corners[i] = inv_corner / inv_corner[3];
      }
      for (uint32_t i = 0; i < 4; i++) {
        float3 dist = frustum_corners[i + 4] - frustum_corners[i];
        frustum_corners[i + 4] = frustum_corners[i] + (dist * split_dist);
        frustum_corners[i] = frustum_corners[i] + (dist * last_split_dist);
      }

      // Calculate frustum center
      float3 center = {0};
      for (uint32_t i = 0; i < 8; i++) {
        center += frustum_corners[i];
      }
      center /= 8.0f;

      // Calculate radius
      float radius = 0.0f;
      for (uint32_t i = 0; i < 8; i++) {
        float distance = magf3(frustum_corners[i] - center);
        radius = SDL_max(radius, distance);
      }
      radius = SDL_ceilf(radius * 16.0f) / 16.0f;

      const float3 max = {radius, radius, radius};
      const float3 min = -max;

      // Calculate projection
      float4x4 proj =
          orthographic(max[0], min[0], max[1], min[1], 0.0f, max[2] - min[2]);

      // Calc view matrix
      float4x4 light_view_mat = {.row0 = {0}};
      {
        const float3 forward = transform_get_forward(&transform);

        const float3 offset = center + (forward * -min[2]);
        look_at(&light_view_mat, offset, center, (float3){0, 1, 0});
      }

      // Calculate view projection matrix
      data.vp = mulmf44(proj, light_view_mat);

      // Inverse
      data.inv_vp = inv_mf44(data.vp);

      Frustum frustum = frustum_from_view_proj(&data.vp);

      tb_view_system_set_view_data(
          view_system, dir_light->cascade_views[cascade_idx], &data);
      tb_view_system_set_view_frustum(
          view_system, dir_light->cascade_views[cascade_idx], &frustum);

      // Store cascade info
      out_lights->cascade_splits[cascade_idx] =
          (near + split_dist * clip_range) * -1.0f;

      last_split_dist = split_dist;
    }
  }

  // Write out the directional light components so we store the cascade splits
  {
    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .components = (uint8_t *)out_lights,
        .count = light_count,
        .entities = light_entities,
        .id = DirectionalLightComponentId,
    };
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
      .dep_count = 2,
      .deps[0] = {.count = 2,
                  .dependent_ids = {DirectionalLightComponentId,
                                    TransformComponentId}},
      .deps[1] = {.count = 2,
                  .dependent_ids = {CameraComponentId, TransformComponentId}},
      .system_dep_count = 2,
      .system_deps[0] = ViewSystemId,
      .system_deps[1] = VisualLoggingSystemId,
      .create = tb_create_shadow_system,
      .destroy = tb_destroy_shadow_system,
      .tick = tb_tick_shadow_system,
  };
}
