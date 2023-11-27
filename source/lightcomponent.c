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

  ECS_COMPONENT(ecs, TbViewSystem);
  ECS_COMPONENT(ecs, TbDirectionalLightComponent);

  TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  bool ret = true;
  if (node->light) {
    const cgltf_light *light = node->light;

    if (light->type == cgltf_light_type_directional) {
      TbDirectionalLightComponent comp = {
          .color = {light->color[0], light->color[1], light->color[2]},
      };
      for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
        comp.cascade_views[i] = tb_view_system_create_view(view_sys);
      }
      ecs_set_ptr(ecs, e, TbDirectionalLightComponent, &comp);

      ecs_singleton_modified(ecs, TbViewSystem);
    }
  }
  return ret;
}

void remove_dir_light_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TbDirectionalLightComponent);

  // Remove light component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(TbDirectionalLightComponent)},
                              },
                      });

  ecs_iter_t light_it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&light_it)) {
    TbDirectionalLightComponent *lights =
        ecs_field(&light_it, TbDirectionalLightComponent, 1);

    for (int32_t i = 0; i < light_it.count; ++i) {
      TbDirectionalLightComponent *light = &lights[i];
      *light = (TbDirectionalLightComponent){.color = tb_f3(0, 0, 0)};
    }
  }

  ecs_filter_fini(filter);
}

void tb_register_light_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TbAssetSystem);
  ECS_COMPONENT(ecs, TbLightSystem);
  // Register an asset system for handling lights
  TbAssetSystem asset = {
      .add_fn = create_dir_light_component,
      .rem_fn = remove_dir_light_components,
  };
  ecs_set_ptr(ecs, ecs_id(TbLightSystem), TbAssetSystem, &asset);
}
