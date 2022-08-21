#pragma once

#include "allocator.h"

typedef struct World World;

typedef uint64_t EntityId;
typedef uint64_t Entity;

typedef const void *InternalDescriptor;

typedef uint64_t ComponentId;
typedef bool (*ComponentCreateFn)(void *self);
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
  InternalDescriptor desc;
  SystemComponentDependencies deps;
  SystemCreateFn create;
  SystemDestroyFn destroy;
  SystemTickFn tick;
} SystemDescriptor;
typedef struct System {
  const char *name;
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

  uint64_t entity_count;
  Entity *entities;
  uint64_t max_entities;

  uint32_t component_store_count;
  ComponentStore *component_stores;

  uint32_t system_count;
  System *systems;

} World;

bool tb_create_world(const WorldDescriptor *desc, World *world);
void tb_tick_world(World *world, float delta_seconds);
void tb_destroy_world(World *world);

EntityId tb_add_entity(World *world, uint32_t comp_count,
                       const ComponentId *components);
bool tb_remove_entity(World *world, EntityId id);
