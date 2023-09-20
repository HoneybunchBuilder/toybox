#include "noclipcomponent.h"

#include "assetsystem.h"
#include "json.h"
#include "noclipcontrollersystem.h"
#include "tbgltf.h"

#include <SDL2/SDL_stdinc.h>

#include <flecs.h>

bool create_noclip_comp(ecs_world_t *ecs, ecs_entity_t e,
                        const char *source_path, const cgltf_node *node,
                        json_object *extra) {
  (void)source_path;
  (void)node;
  if (extra) {
    bool noclip = false;
    json_object_object_foreach(extra, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, NoClipComponentIdStr) == 0) {
          noclip = true;
          break;
        }
      }
    }
    if (noclip) {
      ECS_COMPONENT(ecs, NoClipComponent);
      NoClipComponent comp = {0};
      json_object_object_foreach(extra, key, value) {
        if (SDL_strcmp(key, "move_speed") == 0) {
          comp.move_speed = (float)json_object_get_double(value);
        } else if (SDL_strcmp(key, "look_speed") == 0) {
          comp.look_speed = (float)json_object_get_double(value);
        }
      }
      ecs_set_ptr(ecs, e, NoClipComponent, &comp);
    }
  }
  return true;
}

void remove_noclip_comps(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, NoClipComponent);

  // Remove noclip component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(NoClipComponent)},
                              },
                      });

  ecs_iter_t it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&it)) {
    NoClipComponent *noclip = ecs_field(&it, NoClipComponent, 1);
    for (int32_t i = 0; i < it.count; ++i) {
      *noclip = (NoClipComponent){0};
      ecs_remove(ecs, it.entities[i], NoClipComponent);
    }
  }
  ecs_filter_fini(filter);
}

void tb_register_noclip_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, NoClipControllerSystem);
  // Add an AssetSystem to the no clip singleton in order
  // to allow for component parsing
  AssetSystem asset = {
      .add_fn = create_noclip_comp,
      .rem_fn = remove_noclip_comps,
  };
  ecs_set_ptr(ecs, ecs_id(NoClipControllerSystem), AssetSystem, &asset);
}
