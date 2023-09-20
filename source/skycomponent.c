#include "skycomponent.h"

#include "assetsystem.h"
#include "skysystem.h"

#include <json.h>

#include <flecs.h>

bool create_sky_component(ecs_world_t *ecs, ecs_entity_t e,
                          const char *source_path, const cgltf_node *node,
                          json_object *extra) {
  (void)source_path;
  (void)node;

  ECS_COMPONENT(ecs, SkyComponent);

  bool ret = true;
  if (extra) {
    bool sky = false;
    json_object_object_foreach(extra, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, SkyComponentIdStr) == 0) {
          sky = true;
          break;
        }
      }
    }

    if (sky) {
      SkyComponent sky = {0};
      json_object_object_foreach(extra, key, value) {
        if (SDL_strcmp(key, "cirrus") == 0) {
          sky.cirrus = (float)json_object_get_double(value);
        } else if (SDL_strcmp(key, "cumulus") == 0) {
          sky.cumulus = (float)json_object_get_double(value);
        }
      }
      ecs_set_ptr(ecs, e, SkyComponent, &sky);
    }
  }
  return ret;
}

void remove_sky_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, SkyComponent);

  // Remove light component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(SkyComponent)},
                              },
                      });

  ecs_iter_t it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&it)) {
    SkyComponent *skies = ecs_field(&it, SkyComponent, 1);

    for (int32_t i = 0; i < it.count; ++i) {
      SkyComponent *sky = &skies[i];
      *sky = (SkyComponent){0};
    }
  }

  ecs_filter_fini(filter);
}

void tb_register_sky_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, SkySystem);
  // Register a system for loading skies
  AssetSystem asset = {
      .add_fn = create_sky_component,
      .rem_fn = remove_sky_components,
  };
  ecs_set_ptr(ecs, ecs_id(SkySystem), AssetSystem, &asset);
}
