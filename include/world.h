#pragma once

#include "allocator.h"

#define TB_DEFINE_COMPONENT(lower_name, self_type, desc_type)                  \
  bool tb_create_##lower_name##_component(void *self, InternalDescriptor desc, \
                                          uint32_t system_dep_count,           \
                                          System *const *system_deps) {        \
    return create_##lower_name##_component((self_type *)self,                  \
                                           (const desc_type *)desc,            \
                                           system_dep_count, system_deps);     \
  }                                                                            \
                                                                               \
  void tb_destroy_##lower_name##_component(                                    \
      void *self, uint32_t system_dep_count, System *const *system_deps) {     \
    destroy_##lower_name##_component((self_type *)self, system_dep_count,      \
                                     system_deps);                             \
  }

#define TB_DEFINE_SYSTEM(lower_name, self_type, desc_type)                     \
  bool create_##lower_name##_system(self_type *self, const desc_type *desc,    \
                                    uint32_t system_dep_count,                 \
                                    System *const *system_deps);               \
  void destroy_##lower_name##_system(self_type *self);                         \
  void tick_##lower_name##_system(self_type *self, const SystemInput *input,   \
                                  SystemOutput *output, float delta_seconds);  \
                                                                               \
  bool tb_create_##lower_name##_system(void *self, InternalDescriptor desc,    \
                                       uint32_t system_dep_count,              \
                                       System *const *system_deps) {           \
    return create_##lower_name##_system((self_type *)self,                     \
                                        (const desc_type *)desc,               \
                                        system_dep_count, system_deps);        \
  }                                                                            \
                                                                               \
  void tb_destroy_##lower_name##_system(void *self) {                          \
    destroy_##lower_name##_system((self_type *)self);                          \
  }                                                                            \
                                                                               \
  void tb_tick_##lower_name##_system(void *self, const SystemInput *input,     \
                                     SystemOutput *output,                     \
                                     float delta_seconds) {                    \
    tick_##lower_name##_system((self_type *)self, input, output,               \
                               delta_seconds);                                 \
  }

typedef struct World World;

typedef const void *InternalDescriptor;

typedef uint32_t EntityId;
typedef uint32_t ComponentId;

static const uint32_t InvalidEntityId = 0xFFFF;
static const uint32_t InvalidComponentId = 0xFFFF;

#define ComponentIdAsStr(id) "\"" #id "\""

#define MAX_COMPONENT_DEP_COUNT 4
#define MAX_COLUMN_COUNT MAX_COMPONENT_DEP_COUNT
#define MAX_DEPENDENCY_SET_COUNT 4
#define MAX_SYSTEM_DEP_COUNT 8
#define MAX_OUTPUT_SET_COUNT 4

typedef uint32_t Entity;
typedef struct EntityDescriptor {
  const char *name;
  uint32_t component_count;
  const ComponentId *component_ids;
  const InternalDescriptor *component_descriptors;
} EntityDescriptor;

typedef uint64_t SystemId;
typedef struct System System;

typedef bool (*ComponentCreateFn)(void *self, InternalDescriptor desc,
                                  uint32_t system_dep_count,
                                  System *const *system_deps);
typedef void (*ComponentDestroyFn)(void *self, uint32_t system_dep_count,
                                   System *const *system_deps);
typedef struct ComponentDescriptor {
  const char *name;
  uint64_t size;
  ComponentId id;

  uint32_t system_dep_count;
  SystemId system_deps[MAX_SYSTEM_DEP_COUNT];

  ComponentCreateFn create;
  ComponentDestroyFn destroy;
} ComponentDescriptor;

typedef struct ComponentStore {
  const char *name;
  ComponentId id;
  uint64_t size;       // size of the component element
  uint32_t count;      // Number of components in
                       // the collection
  uint8_t *components; // Component storage in
                       // generic bytes
  ComponentDescriptor desc;
  ComponentCreateFn create;
  ComponentDestroyFn destroy;
} ComponentStore;

typedef struct SystemComponentDependencies {
  uint32_t count;
  ComponentId dependent_ids[MAX_COMPONENT_DEP_COUNT];
} SystemComponentDependencies;

typedef struct PackedComponentStore {
  ComponentId id;
  uint8_t *components;
} PackedComponentStore;

typedef struct SystemDependencySet {
  uint32_t column_count;
  PackedComponentStore columns[MAX_COLUMN_COUNT];
  uint32_t entity_count;
  EntityId *entity_ids;
} SystemDependencySet;

typedef struct SystemInput {
  uint32_t dep_set_count;
  SystemDependencySet dep_sets[MAX_DEPENDENCY_SET_COUNT];
} SystemInput;

// The idea is that the system tick can write
// to this object Memory for dynamic members
// is expected to come from a temporary
// allocator components and entities are
// expected to be the same array lengths
// (count).
typedef struct SystemWriteSet {
  ComponentId id;
  uint32_t count;
  uint8_t *components;
  const EntityId *entities;
} SystemWriteSet;

typedef struct SystemOutput {
  uint32_t set_count;
  SystemWriteSet write_sets[MAX_OUTPUT_SET_COUNT];
} SystemOutput;

typedef bool (*SystemCreateFn)(void *self, InternalDescriptor desc,
                               uint32_t system_dep_count,
                               System *const *system_deps);
typedef void (*SystemDestroyFn)(void *self);
typedef void (*SystemTickFn)(void *self, const SystemInput *input,
                             SystemOutput *output, float delta_seconds);
typedef struct SystemDescriptor {
  const char *name;
  uint64_t size;
  SystemId id;
  InternalDescriptor desc;

  uint32_t dep_count;
  SystemComponentDependencies deps[MAX_DEPENDENCY_SET_COUNT];

  uint32_t system_dep_count;
  SystemId system_deps[MAX_SYSTEM_DEP_COUNT];

  SystemCreateFn create;
  SystemDestroyFn destroy;
  SystemTickFn tick;
} SystemDescriptor;
typedef struct System {
  const char *name;
  SystemId id;

  uint32_t dep_count;
  SystemComponentDependencies deps[MAX_DEPENDENCY_SET_COUNT];

  uint32_t system_dep_count;
  System *system_deps[MAX_SYSTEM_DEP_COUNT];

  void *self;
  SystemCreateFn create;
  SystemDestroyFn destroy;
  SystemTickFn tick;
} System;

typedef struct WorldDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;

  uint32_t component_count;
  const ComponentDescriptor *component_descs;

  uint32_t system_count;
  const SystemDescriptor *system_descs;
  const SystemId *init_order;
  const SystemId *tick_order;

} WorldDescriptor;

typedef struct World {
  Allocator std_alloc;
  Allocator tmp_alloc;

  uint32_t entity_count;
  Entity *entities;
  uint32_t max_entities;

  uint32_t component_store_count;
  ComponentStore *component_stores;

  uint32_t system_count;
  System *systems;
  uint32_t *init_order;
  uint32_t *tick_order;

} World;

bool tb_create_world(const WorldDescriptor *desc, World *world);
bool tb_tick_world(World *world, float delta_seconds);
void tb_destroy_world(World *world);

bool tb_world_load_scene(World *world, const char *scene_path);

EntityId tb_world_add_entity(World *world, const EntityDescriptor *desc);
bool tb_world_remove_entity(World *world, EntityId id);

const PackedComponentStore *tb_get_column_check_id(const SystemInput *input,
                                                   uint32_t set, uint32_t index,
                                                   ComponentId id);
EntityId *tb_get_column_entity_ids(const SystemInput *input, uint32_t set);
uint32_t tb_get_column_component_count(const SystemInput *input, uint32_t set);

System *tb_find_system_by_id(System *systems, uint32_t system_count,
                             SystemId id);
System *tb_find_system_dep_by_id(System *const *systems, uint32_t system_count,
                                 SystemId id);
void *tb_find_system_dep_self_by_id(System *const *systems,
                                    uint32_t system_count, SystemId id);

// Helper API
#define tb_get_component(store, idx, Type)                                     \
  &((const Type *)(store)->components)[(idx)];
#define tb_get_system(deps, count, Type)                                       \
  (Type *)tb_find_system_dep_self_by_id(deps, count, Type##Id);
