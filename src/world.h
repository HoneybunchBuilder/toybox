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
  void tb_tick_##lower_name##_system(SystemDependencyColumns *columns,         \
                                     void *self, float delta_seconds) {        \
    tick_##lower_name##_system(columns, (self_type *)self, delta_seconds);     \
  }

typedef struct World World;

typedef const void *InternalDescriptor;

typedef uint32_t EntityId;
typedef uint32_t ComponentId;

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

#define MAX_COMPONENT_DEP_COUNT 16
typedef struct SystemComponentDependencies {
  uint32_t count;
  ComponentId dependent_ids[MAX_COMPONENT_DEP_COUNT];
} SystemComponentDependencies;

typedef struct PackedComponentStore {
  ComponentId id;
  uint32_t count;
  uint8_t *components;
  EntityId *entity_ids;
} PackedComponentStore;

typedef struct SystemDependencyColumns {
  uint32_t count;
  PackedComponentStore const *columns[MAX_COMPONENT_DEP_COUNT];
} SystemDependencyColumns;

typedef uint64_t SystemId;
typedef bool (*SystemCreateFn)(void *self, InternalDescriptor desc);
typedef void (*SystemDestroyFn)(void *self);
typedef void (*SystemTickFn)(SystemDependencyColumns *columns, void *self,
                             float delta_seconds);
typedef struct SystemDescriptor {
  const char *name;
  uint64_t size;
  SystemId id;
  InternalDescriptor desc;
  SystemComponentDependencies deps;
  SystemCreateFn create;
  SystemDestroyFn destroy;
  SystemTickFn tick;
} SystemDescriptor;
typedef struct System {
  const char *name;
  SystemId id;
  SystemComponentDependencies deps;
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
