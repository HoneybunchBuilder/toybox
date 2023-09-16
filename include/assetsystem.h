#pragma once

typedef struct ecs_world_t ecs_world_t;
typedef uint64_t ecs_entity_t;
typedef struct cgltf_node cgltf_node;
typedef struct json_object json_object;

typedef bool (*ComponentAddFn)(ecs_world_t *ecs, ecs_entity_t e,
                               const char *source_path, const cgltf_node *node,
                               json_object *extra);

typedef void (*ComponentRemoveFn)(ecs_world_t *ecs);

// A type of system that the world cares about
typedef struct AssetSystem {
  uint32_t id;
  const char *id_str;
  ComponentAddFn add_fn;
  ComponentRemoveFn rem_fn;
} AssetSystem;
