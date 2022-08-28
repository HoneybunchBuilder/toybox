#pragma once

#include "allocator.h"

#define TB_DEFINE_COMPONENT(lower_name, self_type, desc_type)                  \
  bool tb_create_##lower_name##_component(void *self,                          \
                                          InternalDescriptor desc) {           \
    return create_##lower_name##_component((self_type *)self,                  \
                                           (const desc_type *)desc);           \
  }                                                                            \
                                                                               \
  void tb_destroy_##lower_name##_component(void *self) {                       \
    destroy_##lower_name##_component((self_type *)self);                       \
  }

#define TB_DEFINE_SYSTEM(lower_name, self_type, desc_type)                     \
  bool tb_create_##lower_name##_system(void *self, InternalDescriptor desc) {  \
    return create_##lower_name##_system((self_type *)self,                     \
                                        (const desc_type *)desc);              \
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

#define ComponentIdAsStr(id) "\"" #id "\""

typedef uint32_t Entity;
typedef struct EntityDescriptor {
  const char *name;
  uint32_t component_count;
  const ComponentId *component_ids;
  const InternalDescriptor *component_descriptors;
} EntityDescriptor;

typedef bool (*ComponentCreateFn)(void *self, InternalDescriptor desc);
typedef void (*ComponentDestroyFn)(void *self);
typedef struct ComponentDescriptor {
  const char *name;
  uint64_t size;
  ComponentId id;
  ComponentCreateFn create;
  ComponentDestroyFn destroy;
} ComponentDescriptor;

typedef struct ComponentStore {
  const char *name;
  ComponentId id;
  uint64_t size;       // size of the component element
  uint32_t count;      // Number of components in the collection
  uint8_t *components; // Component storage in generic bytes
  ComponentCreateFn create;
  ComponentDestroyFn destroy;
} ComponentStore;

#define MAX_COMPONENT_DEP_COUNT 4
#define MAX_COLUMN_COUNT MAX_COMPONENT_DEP_COUNT
#define MAX_DEPENDENCY_SET_COUT 4
#define MAX_OUTPUT_SET_COUNT 4
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
  SystemDependencySet dep_sets[MAX_DEPENDENCY_SET_COUT];
} SystemInput;

// The idea is that the system tick can write to this object
// Memory for dynamic members is expected to come from a temporary allocator
// components and entities are expected to be the same array lengths (count).
typedef struct SystemWriteSet {
  ComponentId id;
  uint32_t count;
  uint8_t *components;
  EntityId *entities;
} SystemWriteSet;

typedef struct SystemOutput {
  uint32_t set_count;
  SystemWriteSet write_sets[MAX_OUTPUT_SET_COUNT];
} SystemOutput;

typedef uint64_t SystemId;
typedef bool (*SystemCreateFn)(void *self, InternalDescriptor desc);
typedef void (*SystemDestroyFn)(void *self);
typedef void (*SystemTickFn)(void *self, const SystemInput *input,
                             SystemOutput *output, float delta_seconds);
typedef struct SystemDescriptor {
  const char *name;
  uint64_t size;
  SystemId id;
  InternalDescriptor desc;
  uint32_t dep_count;
  SystemComponentDependencies deps[MAX_DEPENDENCY_SET_COUT];
  SystemCreateFn create;
  SystemDestroyFn destroy;
  SystemTickFn tick;
} SystemDescriptor;
typedef struct System {
  const char *name;
  SystemId id;
  uint32_t dep_count;
  SystemComponentDependencies deps[MAX_DEPENDENCY_SET_COUT];
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

} World;

bool tb_create_world(const WorldDescriptor *desc, World *world);
bool tb_tick_world(World *world, float delta_seconds);
void tb_destroy_world(World *world);

bool tb_world_load_scene(World *world, const char *scene_path);

EntityId tb_world_add_entity(World *world, const EntityDescriptor *desc);
bool tb_world_remove_entity(World *world, EntityId id);
