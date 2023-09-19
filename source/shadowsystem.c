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

void shadow_update_tick(ecs_iter_t *it) {
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

void tb_register_shadow_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, VisualLoggingSystem);
  ECS_COMPONENT(ecs, ShadowSystem);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, TransformComponent);

  ShadowSystem sys = {
      .std_alloc = world->std_alloc,
      .tmp_alloc = world->tmp_alloc,
      .dir_light_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(DirectionalLightComponent)},
                                  {.id = ecs_id(TransformComponent)},
                              }}),
  };

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(ShadowSystem), ShadowSystem, &sys);

  ECS_SYSTEM(ecs, shadow_update_tick, EcsOnUpdate, CameraComponent);
}

void tb_unregister_shadow_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ShadowSystem);
  ShadowSystem *sys = ecs_singleton_get_mut(ecs, ShadowSystem);
  ecs_query_fini(sys->dir_light_query);
  *sys = (ShadowSystem){0};
  ecs_singleton_remove(ecs, ShadowSystem);
}
