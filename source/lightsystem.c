#include "lightsystem.h"

#include "assetsystem.h"
#include "cameracomponent.h"
#include "profiling.h"
#include "tbgltf.h"
#include "transformcomponent.h"
#include "viewsystem.h"

#include <flecs.h>

void light_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Light System", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, LightSystem);
  ECS_COMPONENT(ecs, ViewSystem);

  const LightSystem *light_sys = ecs_singleton_get(ecs, LightSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  CameraComponent *cameras = ecs_field(it, CameraComponent, 1);

  for (int32_t cam_idx = 0; cam_idx < it->count; ++cam_idx) {
    CameraComponent *camera = &cameras[cam_idx];

    ecs_iter_t light_it = ecs_query_iter(ecs, light_sys->dir_light_query);
    while (ecs_iter_next(&light_it)) {
      DirectionalLightComponent *lights =
          ecs_field(&light_it, DirectionalLightComponent, 1);
      TransformComponent *transforms =
          ecs_field(&light_it, TransformComponent, 2);

      for (int32_t light_idx = 0; light_idx < light_it.count; ++light_idx) {
        DirectionalLightComponent *light = &lights[light_idx];
        TransformComponent *transform = &transforms[light_idx];

        float3 dir = -transform_get_forward(&transform->transform);

        // Send lighting data to the camera's view
        CommonLightData light_data = {
            .color = light->color,
            .light_dir = dir,
            .cascade_splits = light->cascade_splits,
        };
        for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
          const View *cascade_view =
              tb_get_view(view_sys, light->cascade_views[i]);
          light_data.cascade_vps[i] = cascade_view->view_data.vp;
        }
        tb_view_system_set_light_data(view_sys, camera->view_id, &light_data);
      }
    }
  }
  TracyCZoneEnd(ctx);
}

void tb_register_light_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, LightSystem);
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, CameraComponent);

  LightSystem sys = {.dir_light_query = ecs_query(
                         ecs, {.filter.terms = {
                                   {.id = ecs_id(DirectionalLightComponent)},
                                   {.id = ecs_id(TransformComponent)},
                               }})};

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(LightSystem), LightSystem, &sys);

  ECS_SYSTEM(ecs, light_update_tick, EcsOnUpdate, [in] CameraComponent);

  tb_register_light_component(ecs);
}

void tb_unregister_light_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, LightSystem);
  LightSystem *sys = ecs_singleton_get_mut(ecs, LightSystem);
  ecs_query_fini(sys->dir_light_query);
  ecs_singleton_remove(ecs, LightSystem);
}
