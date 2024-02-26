#include "lightcomponent.h"

#include "lightsystem.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

ECS_COMPONENT_DECLARE(TbDirectionalLightComponent);

bool tb_load_light_comp(TbWorld *world, ecs_entity_t ent,
                        const char *source_path, const cgltf_node *node,
                        json_object *json) {
  (void)source_path;
  (void)json;

  ecs_world_t *ecs = world->ecs;

  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto light = node->light;

  if (light->type == cgltf_light_type_directional) {
    TbDirectionalLightComponent comp = {
        .color = tb_atof3(light->color),
    };
    for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
      comp.cascade_views[i] = tb_view_system_create_view(view_sys);
    }
    ecs_set_ptr(ecs, ent, TbDirectionalLightComponent, &comp);

    ecs_singleton_modified(ecs, TbViewSystem);
  }
  return true;
}

ecs_entity_t tb_register_light_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbDirectionalLightComponent);
  // Returning 0 means we need no custom descriptor for editor UI
  return 0;
}

TB_REGISTER_COMP(tb, light)
