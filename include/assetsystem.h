#pragma once

#include <SDL2/SDL_stdinc.h>
#include <stdbool.h>

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;
typedef struct cgltf_node cgltf_node;
typedef struct json_object json_object;

typedef bool (*TbComponentAddFn)(ecs_world_t *ecs, ecs_entity_t e,
                                 const char *source_path,
                                 const cgltf_node *node, json_object *extra);
typedef void (*TbComponentPostLoadFn)(ecs_world_t *ecs, ecs_entity_t e);
typedef void (*TbComponentRemoveFn)(ecs_world_t *ecs);

// A type of system that the world cares about
typedef struct TbAssetSystem {
  TbComponentAddFn add_fn;
  TbComponentPostLoadFn post_load_fn;
  TbComponentRemoveFn rem_fn;
} TbAssetSystem;
