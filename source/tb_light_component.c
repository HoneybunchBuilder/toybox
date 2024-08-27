#include "tb_light_component.h"

#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_light_system.h"
#include "tb_view_system.h"
#include "tb_world.h"

#include <flecs.h>

ECS_COMPONENT_DECLARE(TbDirectionalLightComponent);

bool tb_load_light_comp(ecs_world_t *ecs, ecs_entity_t ent,
                        const char *source_path, const cgltf_data *data,
                        const cgltf_node *node, json_object *json) {
  (void)source_path;
  (void)data;
  (void)json;

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

TbComponentRegisterResult tb_register_light_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbDirectionalLightComponent);
  // Returning 0 means we need no custom descriptor for editor UI
  return (TbComponentRegisterResult){ecs_id(TbDirectionalLightComponent), 0};
}

bool tb_ready_light_comp(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto comp = ecs_get(ecs, ent, TbDirectionalLightComponent);
  return comp != NULL;
}

TB_REGISTER_COMP(tb, light)
