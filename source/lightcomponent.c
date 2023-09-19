#include "lightcomponent.h"

#include "assetsystem.h"
#include "lightsystem.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

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

void tb_register_light_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, LightSystem);
  // Register an asset system for handling lights
  AssetSystem asset = {
      .add_fn = create_dir_light_component,
      .rem_fn = remove_dir_light_components,
  };
  ecs_set_ptr(ecs, ecs_id(LightSystem), AssetSystem, &asset);
}
