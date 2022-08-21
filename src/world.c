#include "world.h"

#include "allocator.h"
#include "profiling.h"
#include "tbcommon.h"

#include "transformcomponent.h"

void create_component_store(ComponentStore *store,
                            const ComponentDescriptor *desc) {
  store->name = desc->name;
  store->id = desc->id;
  store->size = desc->size;
  store->count = 0;
  store->components = NULL;
  store->create = desc->create;
  store->destroy = desc->destroy;
}

bool create_system(World *world, System *system, const SystemDescriptor *desc) {
  system->name = desc->name;
  system->deps = desc->deps;
  system->create = desc->create;
  system->destroy = desc->destroy;
  system->tick = desc->tick;

  // Allocate and initialize each system
  system->self = tb_alloc(world->std_alloc, desc->size);
  TB_CHECK_RETURN(system->self, "Failed to allocate system.", false);

  bool created = system->create(system->self, world);
  TB_CHECK_RETURN(created, "Failed to create system internals.", false);

  return true;
}

bool tb_create_world(const WorldDescriptor *desc, World *world) {
  if (desc == NULL || world == NULL) {
    return false;
  }

  // Assign allocators
  Allocator std_alloc = desc->std_alloc;
  Allocator tmp_alloc = desc->tmp_alloc;

  world->std_alloc = std_alloc;
  world->tmp_alloc = tmp_alloc;

  // Create component stores
  {
    const uint64_t store_count = desc->component_count;
    ComponentStore *stores = NULL;
    if (store_count > 0) {
      stores = tb_alloc_nm_tp(std_alloc, store_count, ComponentStore);
      TB_CHECK_RETURN(stores, "Failed to allocate component stores.", false);

      for (uint64_t i = 0; i < store_count; ++i) {
        const ComponentDescriptor *comp_desc = &desc->component_descs[i];
        create_component_store(&stores[i], comp_desc);
      }
    }

    world->component_store_count = store_count;
    world->component_stores = stores;
  }

  // Create systems
  {
    const uint64_t system_count = desc->system_count;
    System *systems = NULL;
    if (system_count > 0) {
      systems = tb_alloc_nm_tp(std_alloc, system_count, System);
      TB_CHECK_RETURN(systems, "Failed to allocate systems.", false);

      bool system_created = false;
      for (uint64_t i = 0; i < system_count; ++i) {
        const SystemDescriptor *sys_desc = &desc->system_descs[i];
        system_created = create_system(world, &systems[i], sys_desc);
        TB_CHECK_RETURN(system_created, "Failed to create system.", false);
      }
    }

    world->system_count = system_count;
    world->systems = systems;
  }

  return true;
}

void tb_tick_world(World *world, float delta_seconds) {
  TracyCZoneN(world_tick_ctx, "World Tick", true);
  TracyCZoneColor(world_tick_ctx, TracyCategoryColorCore);

  Allocator tmp_alloc = world->tmp_alloc;

  // Gather packed columns for each component type
  const uint64_t store_count = world->component_store_count;
  PackedComponentStore *packed_stores = NULL;
  if (store_count > 0) {
    TracyCZoneN(pack_ctx, "Pack Columns", true);
    TracyCZoneColor(pack_ctx, TracyCategoryColorCore);

    // Allocate the list of packed stores on the temp allocator
    // We just need it for this frame
    packed_stores =
        tb_alloc_nm_tp(tmp_alloc, store_count, PackedComponentStore);

    for (uint64_t store_idx = 0; store_idx < world->component_store_count;
         ++store_idx) {
      const ComponentStore *store = &world->component_stores[store_idx];
      const uint64_t comp_size = store->size;

      // Get the packed store that corresponds with this current store
      PackedComponentStore *packed_store = &packed_stores[store_idx];
      packed_store->id = store->id;
      packed_store->components =
          tb_alloc(tmp_alloc, store->size * store->count);
      packed_store->count = 0;

      // Copy components from the core store to the packed store
      uint32_t packed_comp_idx = 0;
      for (uint32_t comp_idx = 0; comp_idx < world->max_entities; ++comp_idx) {
        Entity entity = world->entities[comp_idx];
        // If the entity is marked to use this component
        if ((entity & (1 << comp_idx)) == 1) {
          uint8_t *component = &store->components[comp_idx * comp_size];
          uint8_t *packed_comp_dst =
              &packed_store->components[packed_comp_idx * comp_size];
          SDL_memcpy(packed_comp_dst, component, comp_size);
          packed_store->count++;
        }
      }
    }

    TracyCZoneEnd(pack_ctx);
  }

  {
    TracyCZoneN(system_tick_ctx, "Tick Systems", true);
    TracyCZoneColor(system_tick_ctx, TracyCategoryColorCore);
    for (uint64_t system_idx = 0; system_idx < world->system_count;
         ++system_idx) {
      System *system = &world->systems[system_idx];

      // Gather packed component columns for this system
      SystemDependencyColumns columns = {0};
      {
        const SystemComponentDependencies deps = system->deps;
        for (uint32_t column_idx = 0; column_idx < deps.count; ++column_idx) {
          const ComponentId comp_id = deps.dependent_ids[column_idx];
          for (uint32_t store_idx = 0; store_idx < world->component_store_count;
               ++store_idx) {
            if (packed_stores[store_idx].id == comp_id) {
              columns.columns[columns.count++] = &packed_stores[store_idx];
            }
          }
        }
      }

      system->tick(&columns, system->self, delta_seconds);
    }
    TracyCZoneEnd(system_tick_ctx);
  }

  TracyCZoneEnd(world_tick_ctx);
}

void tb_destroy_world(World *world) {
  if (world->component_store_count > 0) {
    for (uint64_t i = 0; i < world->component_store_count; ++i) {
      ComponentStore *store = &world->component_stores[i];
      for (uint64_t comp_idx = 0; comp_idx < world->entity_count; ++comp_idx) {
        uint8_t *comp = &store->components[(store->size * comp_idx)];
        if (comp) {
          store->destroy(comp);
        }
      }
    }
    tb_free(world->std_alloc, world->component_stores);
  }

  if (world->system_count > 0) {
    for (uint64_t i = 0; i < world->system_count; ++i) {
      System *system = &world->systems[i];
      system->destroy(system->self);
      tb_free(world->std_alloc, system->self);
    }
    tb_free(world->std_alloc, world->systems);
  }

  *world = (World){0};
}

EntityId tb_add_entity(World *world, uint32_t comp_count,
                       const ComponentId *components) {
  TracyCZoneN(ctx, "Add Entity", true);
  TracyCZoneColor(ctx, TracyCategoryColorCore);

  // Determine if we need to grow the entity list
  const uint64_t new_entity_count = world->entity_count + 1;
  if (new_entity_count > world->max_entities) {
    world->max_entities = new_entity_count * 2;
    world->entities = tb_realloc_nm_tp(world->std_alloc, world->entities,
                                       world->max_entities, Entity);

    // Also resize *all* the component stores
    for (uint32_t store_idx = 0; store_idx < world->component_store_count;
         ++store_idx) {
      ComponentStore *store = &world->component_stores[store_idx];
      store->components =
          tb_realloc_nm_tp(world->std_alloc, store->components,
                           store->size * world->max_entities, uint8_t);
    }
  }
  EntityId entity_id = world->entity_count;
  Entity *entity = &world->entities[entity_id];
  world->entity_count++;

  for (uint32_t store_idx = 0; store_idx < world->component_store_count;
       ++store_idx) {
    ComponentStore *store = &world->component_stores[store_idx];

    // Determine if this component store will be referenced by this entity
    for (uint32_t comp_idx = 0; comp_idx < comp_count; ++comp_idx) {
      if (components[comp_idx] == store->id) {
        // Mark this store as being used by the entity
        (*entity) |= (1 << store_idx);
        store->count++;

        // Create a component in the store at this entity index
        uint8_t *comp_head = &store->components[entity_id * store->size];
        if (!store->create(comp_head)) {
          SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "%s",
                       "Failed to create component for entity.");
          SDL_TriggerBreakpoint();
          TracyCZoneEnd(ctx);
          return (EntityId)-1;
        }

        break;
      }
    }
  }

  TracyCZoneEnd(ctx);
  return entity_id;
}

bool tb_remove_entity(World *world, EntityId id) {
  (void)world;
  (void)id;
  return false;
}
