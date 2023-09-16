#include "lightsystem.h"

#include "assetsystem.h"
#include "cameracomponent.h"
#include "tbgltf.h"
#include "transformcomponent.h"
#include "viewsystem.h"

#include <flecs.h>

void light_update_tick(ecs_iter_t *it) {
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, LightSysContext);
  ECS_COMPONENT(ecs, ViewSystem);

  const LightSysContext *light_sys = ecs_singleton_get(ecs, LightSysContext);
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
}

bool create_dir_light_component(ecs_world_t *ecs, ecs_entity_t e,
                                const char *source_path, const cgltf_node *node,
                                json_object *extra) {
  (void)source_path;
  (void)extra;

  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, DirectionalLightComponent);

  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  bool ret = true;
  if (node->light) {
    const cgltf_light *light = node->light;

    if (light->type == cgltf_light_type_directional) {
      DirectionalLightComponent comp = {
          .color = {light->color[0], light->color[1], light->color[2]},
      };
      for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
        comp.cascade_views[i] = tb_view_system_create_view(view_sys);
      }
      ecs_set_ptr(ecs, e, DirectionalLightComponent, &comp);

      ecs_singleton_modified(ecs, ViewSystem);
    }
  }
  return ret;
}

void remove_dir_light_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, DirectionalLightComponent);

  // Remove light component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(DirectionalLightComponent)},
                              },
                      });

  ecs_iter_t light_it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&light_it)) {
    DirectionalLightComponent *lights =
        ecs_field(&light_it, DirectionalLightComponent, 1);

    for (int32_t i = 0; i < light_it.count; ++i) {
      DirectionalLightComponent *light = &lights[i];
      *light = (DirectionalLightComponent){.color = f3(0, 0, 0)};
    }
  }

  ecs_filter_fini(filter);
}

void tb_register_light_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, LightSysContext);
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, CameraComponent);

  LightSysContext sys = {
      .dir_light_query =
          ecs_query(ecs, {.filter.terms = {
                              {.id = ecs_id(DirectionalLightComponent)},
                              {.id = ecs_id(TransformComponent)},
                          }})};

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(LightSysContext), LightSysContext, &sys);

  ECS_SYSTEM(ecs, light_update_tick, EcsOnUpdate, [in] CameraComponent);

  // Register an asset system for handling lights
  AssetSystem asset = {
      .add_fn = create_dir_light_component,
      .rem_fn = remove_dir_light_components,
  };
  ecs_set_ptr(ecs, ecs_id(LightSysContext), AssetSystem, &asset);
}
void tb_unregister_light_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, LightSysContext);
  LightSysContext *sys = ecs_singleton_get_mut(ecs, LightSysContext);
  ecs_query_fini(sys->dir_light_query);
  ecs_singleton_remove(ecs, LightSysContext);
}
