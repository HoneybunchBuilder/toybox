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

  ECS_COMPONENT(ecs, TbLightSystem);
  ECS_COMPONENT(ecs, TbViewSystem);

  const TbLightSystem *light_sys = ecs_singleton_get(ecs, TbLightSystem);
  TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  TbCameraComponent *cameras = ecs_field(it, TbCameraComponent, 1);

  for (int32_t cam_idx = 0; cam_idx < it->count; ++cam_idx) {
    TbCameraComponent *camera = &cameras[cam_idx];

    ecs_iter_t light_it = ecs_query_iter(ecs, light_sys->dir_light_query);
    while (ecs_iter_next(&light_it)) {
      TbDirectionalLightComponent *lights =
          ecs_field(&light_it, TbDirectionalLightComponent, 1);
      TbTransformComponent *transforms =
          ecs_field(&light_it, TbTransformComponent, 2);

      for (int32_t light_idx = 0; light_idx < light_it.count; ++light_idx) {
        TbDirectionalLightComponent *light = &lights[light_idx];
        TbTransformComponent *transform = &transforms[light_idx];

        float3 dir = -tb_transform_get_forward(&transform->transform);

        // Send lighting data to the camera's view
        TbCommonLightData light_data = {
            .color = light->color,
            .light_dir = dir,
            .cascade_splits = light->cascade_splits,
        };
        for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
          const TbView *cascade_view =
              tb_get_view(view_sys, light->cascade_views[i]);
          light_data.cascade_vps[i] = cascade_view->view_data.vp;
        }
        tb_view_system_set_light_data(view_sys, camera->view_id, &light_data);
      }
    }
  }
  TracyCZoneEnd(ctx);
}

void tb_register_light_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbLightSystem);
  ECS_COMPONENT(ecs, TbAssetSystem);
  ECS_COMPONENT(ecs, TbDirectionalLightComponent);
  ECS_COMPONENT(ecs, TbTransformComponent);

  TbLightSystem sys = {
      .dir_light_query =
          ecs_query(ecs, {.filter.terms = {
                              {.id = ecs_id(TbDirectionalLightComponent)},
                              {.id = ecs_id(TbTransformComponent)},
                          }})};

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbLightSystem), TbLightSystem, &sys);

  ECS_SYSTEM(ecs, light_update_tick, EcsOnUpdate, [in] TbCameraComponent);

  tb_register_light_component(ecs);
}

void tb_unregister_light_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbLightSystem);
  TbLightSystem *sys = ecs_singleton_get_mut(ecs, TbLightSystem);
  ecs_query_fini(sys->dir_light_query);
  ecs_singleton_remove(ecs, TbLightSystem);
}
