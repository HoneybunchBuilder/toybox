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

#include <flecs.h>

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

void tick_shadow_system_internal(ShadowSystem *self, const SystemInput *input,
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

  // Calculate inv cam vp based on shadow draw distance
  float4x4 inv_cam_vp = {.col0 = {0}};
  {
    const View *v = tb_get_view(self->view_system, camera_component->view_id);
    float4 proj_params = v->view_data.proj_params;
    float4x4 view = v->view_data.v;
    float4x4 proj = perspective(proj_params[2], proj_params[3], near, far);
    inv_cam_vp = inv_mf44(mulmf44(proj, view));
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

      float3 frustum_corners[TB_FRUSTUM_CORNER_COUNT] = {{0}};
      // Project into world space
      for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; ++i) {
        const float3 corner = tb_frustum_corners[i];
        float4 inv_corner =
            mulf44(inv_cam_vp, (float4){corner[0], corner[1], corner[2], 1.0f});
        frustum_corners[i] = f4tof3(inv_corner) / inv_corner[3];
      }
      for (uint32_t i = 0; i < 4; i++) {
        float3 dist = frustum_corners[i + 4] - frustum_corners[i];
        frustum_corners[i + 4] = frustum_corners[i] + (dist * split_dist);
        frustum_corners[i] = frustum_corners[i] + (dist * last_split_dist);
      }

      // Calculate frustum center
      float3 center = {0};
      for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; i++) {
        center += frustum_corners[i];
      }
      center /= (float)TB_FRUSTUM_CORNER_COUNT;

      // Calculate radius
      float radius = 0.0f;
      for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; i++) {
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
      float4x4 view = {.col0 = {0}};
      {
        const float3 forward = transform_get_forward(&transform);

        const float3 offset = center + (forward * min[2]);
        // tb_vlog_location(self->vlog, offset, 1.0f, f3(0, 0, 1));
        view = look_at(offset, center, TB_UP);
      }

      // Calculate view projection matrix
      data.v = view;
      data.p = proj;
      data.vp = mulmf44(proj, view);

      // Inverse
      data.inv_vp = inv_mf44(data.vp);
      data.inv_proj = inv_mf44(proj);

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

void tick_shadow_system(void *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick Shadow System");
  tick_shadow_system_internal((ShadowSystem *)self, input, output,
                              delta_seconds);
}

void tb_shadow_system_descriptor(SystemDescriptor *desc,
                                 const ShadowSystemDescriptor *shadow_desc) {
  *desc = (SystemDescriptor){
      .name = "Shadow",
      .size = sizeof(ShadowSystem),
      .id = ShadowSystemId,
      .desc = (InternalDescriptor)shadow_desc,
      .system_dep_count = 2,
      .system_deps[0] = ViewSystemId,
      .system_deps[1] = VisualLoggingSystemId,
      .create = tb_create_shadow_system,
      .destroy = tb_destroy_shadow_system,
      .tick_fn_count = 1,
      .tick_fns[0] =
          {
              .dep_count = 2,
              .deps = {{2, {DirectionalLightComponentId, TransformComponentId}},
                       {2, {CameraComponentId, TransformComponentId}}},
              .system_id = ShadowSystemId,
              .order = E_TICK_PRE_RENDER,
              .function = tick_shadow_system,
          },
  };
}

void flecs_shadow_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Shadow System", TracyCategoryColorCore, true);
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Shadow System Update");
  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, ShadowSystem);
  ECS_COMPONENT(ecs, ViewSystem);

  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  ecs_singleton_modified(ecs, ViewSystem);
  ShadowSystem *shadow_sys = ecs_singleton_get_mut(ecs, ShadowSystem);
  ecs_singleton_modified(ecs, ShadowSystem);

  // For each camera, evaluate each light and calculate any necessary shadow
  // info
  const CameraComponent *cameras = ecs_field(it, CameraComponent, 1);
  for (int32_t cam_idx = 0; cam_idx < it->count; ++cam_idx) {
    const CameraComponent *camera = &cameras[cam_idx];

    const float near = camera->near;
    const float far = camera->far;

    // Calculate inv cam vp based on shadow draw distance
    float4x4 inv_cam_vp = {.col0 = f4(0, 0, 0, 0)};
    {
      const View *v = tb_get_view(view_sys, camera->view_id);
      float4 proj_params = v->view_data.proj_params;
      float4x4 view = v->view_data.v;
      float4x4 proj = perspective(proj_params[2], proj_params[3], near, far);
      inv_cam_vp = inv_mf44(mulmf44(proj, view));
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

    ecs_iter_t light_it = ecs_query_iter(ecs, shadow_sys->dir_light_query);
    while (ecs_query_next(&light_it)) {
      DirectionalLightComponent *lights =
          ecs_field(&light_it, DirectionalLightComponent, 1);
      const TransformComponent *transforms =
          ecs_field(&light_it, TransformComponent, 2);
      for (int32_t light_idx = 0; light_idx < light_it.count; ++light_idx) {
        DirectionalLightComponent *light = &lights[light_idx];
        const TransformComponent *trans = &transforms[light_idx];

        Transform transform = trans->transform;

        CommonViewData data = {
            .view_pos = transform.position,
        };

        float last_split_dist = 0.0f;
        for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
             ++cascade_idx) {
          float split_dist = cascade_splits[cascade_idx];

          float3 frustum_corners[TB_FRUSTUM_CORNER_COUNT] = {{0}};
          // Project into world space
          for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; ++i) {
            const float3 corner = tb_frustum_corners[i];
            float4 inv_corner = mulf44(
                inv_cam_vp, (float4){corner[0], corner[1], corner[2], 1.0f});
            frustum_corners[i] = f4tof3(inv_corner) / inv_corner[3];
          }
          for (uint32_t i = 0; i < 4; i++) {
            float3 dist = frustum_corners[i + 4] - frustum_corners[i];
            frustum_corners[i + 4] = frustum_corners[i] + (dist * split_dist);
            frustum_corners[i] = frustum_corners[i] + (dist * last_split_dist);
          }

          // Calculate frustum center
          float3 center = {0};
          for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; i++) {
            center += frustum_corners[i];
          }
          center /= (float)TB_FRUSTUM_CORNER_COUNT;

          // Calculate radius
          float radius = 0.0f;
          for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; i++) {
            float distance = magf3(frustum_corners[i] - center);
            radius = SDL_max(radius, distance);
          }
          radius = SDL_ceilf(radius * 16.0f) / 16.0f;

          const float3 max = {radius, radius, radius};
          const float3 min = -max;

          // Calculate projection
          float4x4 proj = orthographic(max[0], min[0], max[1], min[1], 0.0f,
                                       max[2] - min[2]);

          // Calc view matrix
          float4x4 view = {.col0 = {0}};
          {
            const float3 forward = transform_get_forward(&transform);

            const float3 offset = center + (forward * min[2]);
            // tb_vlog_location(self->vlog, offset, 1.0f, f3(0, 0, 1));
            view = look_at(offset, center, TB_UP);
          }

          // Calculate view projection matrix
          data.v = view;
          data.p = proj;
          data.vp = mulmf44(proj, view);

          // Inverse
          data.inv_vp = inv_mf44(data.vp);
          data.inv_proj = inv_mf44(proj);

          Frustum frustum = frustum_from_view_proj(&data.vp);

          tb_view_system_set_view_data(
              view_sys, light->cascade_views[cascade_idx], &data);
          tb_view_system_set_view_frustum(
              view_sys, light->cascade_views[cascade_idx], &frustum);

          // Store cascade info
          light->cascade_splits[cascade_idx] =
              (near + split_dist * clip_range) * -1.0f;

          last_split_dist = split_dist;
        }
      }
    }
  }
  TracyCZoneEnd(ctx);
}

void tb_register_shadow_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, VisualLoggingSystem);
  ECS_COMPONENT(ecs, ShadowSystem);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, TransformComponent);

  ShadowSystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .dir_light_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(DirectionalLightComponent)},
                                  {.id = ecs_id(TransformComponent)},
                              }}),
  };

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(ShadowSystem), ShadowSystem, &sys);

  ECS_SYSTEM(ecs, flecs_shadow_tick, EcsOnUpdate, CameraComponent);
}

void tb_unregister_shadow_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ShadowSystem);
  ShadowSystem *sys = ecs_singleton_get_mut(ecs, ShadowSystem);
  ecs_query_fini(sys->dir_light_query);
  destroy_shadow_system(sys);
  ecs_singleton_remove(ecs, ShadowSystem);
}
