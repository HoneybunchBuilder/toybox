#include "world.h"

#include "allocator.h"
#include "cameracomponent.h"
#include "inputsystem.h"
#include "lightcomponent.h"
#include "profiling.h"
#include "simd.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "transformcomponent.h"

static cgltf_result
sdl_read_glb(const struct cgltf_memory_options *memory_options,
             const struct cgltf_file_options *file_options, const char *path,
             cgltf_size *size, void **data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;
  cgltf_size file_size = (cgltf_size)SDL_RWsize(file);
  (void)path;

  void *mem = memory_options->alloc(memory_options->user_data, file_size);
  TB_CHECK_RETURN(mem, "clgtf out of memory.", cgltf_result_out_of_memory);

  TB_CHECK_RETURN(SDL_RWread(file, mem, file_size, 1) != 0, "clgtf io error.",
                  cgltf_result_io_error);

  *size = file_size;
  *data = mem;

  return cgltf_result_success;
}

static void sdl_release_glb(const struct cgltf_memory_options *memory_options,
                            const struct cgltf_file_options *file_options,
                            void *data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;

  memory_options->free(memory_options->user_data, data);

  TB_CHECK(SDL_RWclose(file) == 0, "Failed to close glb file.");
}

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
  const uint32_t dep_count = desc->dep_count > MAX_DEPENDENCY_SET_COUT
                                 ? MAX_DEPENDENCY_SET_COUT
                                 : desc->dep_count;

  system->name = desc->name;
  system->id = desc->id;
  system->dep_count = dep_count;
  SDL_memcpy(system->deps, desc->deps,
             sizeof(SystemComponentDependencies) * dep_count);
  system->create = desc->create;
  system->destroy = desc->destroy;
  system->tick = desc->tick;

  // Allocate and initialize each system1
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
    const uint32_t store_count = desc->component_count;
    ComponentStore *stores = NULL;
    if (store_count > 0) {
      stores = tb_alloc_nm_tp(std_alloc, store_count, ComponentStore);
      TB_CHECK_RETURN(stores, "Failed to allocate component stores.", false);

      for (uint32_t i = 0; i < store_count; ++i) {
        const ComponentDescriptor *comp_desc = &desc->component_descs[i];
        create_component_store(&stores[i], comp_desc);
      }
    }

    world->component_store_count = store_count;
    world->component_stores = stores;
  }

  // Create systems
  {
    const uint32_t system_count = desc->system_count;
    System *systems = NULL;
    if (system_count > 0) {
      systems = tb_alloc_nm_tp(std_alloc, system_count, System);
      TB_CHECK_RETURN(systems, "Failed to allocate systems.", false);

      bool system_created = false;
      for (uint32_t i = 0; i < system_count; ++i) {
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

bool tb_tick_world(World *world, float delta_seconds) {
  TracyCZoneN(world_tick_ctx, "World Tick", true);
  TracyCZoneColor(world_tick_ctx, TracyCategoryColorCore);

  Allocator tmp_alloc = world->tmp_alloc;

  /*
    TODO: A better approach here would be:
    For each system
    {
      Find the set of entity ids where all components required by the system are
    enabled.
      Pack together a structure on the temp allocator that is a table
    where each column is a linear array. The first column stores entity ids on
    the global world table and each subsequent column is the packed array of
    that component type.
      Pass that as input to the system's tick
    }

    We have to do this packing per-system anyway since each system could change
    the world. Still have to figure out how to handle systems writing to the
    world.
  */

  // Gather packed columns for each component type
  const uint32_t store_count = world->component_store_count;
  PackedComponentStore *packed_stores = NULL;
  if (store_count > 0) {
    TracyCZoneN(pack_ctx, "Pack Columns", true);
    TracyCZoneColor(pack_ctx, TracyCategoryColorCore);

    // Allocate the list of packed stores on the temp allocator
    // We just need it for this frame
    packed_stores =
        tb_alloc_nm_tp(tmp_alloc, store_count, PackedComponentStore);

    for (uint32_t store_idx = 0; store_idx < world->component_store_count;
         ++store_idx) {
      const ComponentStore *store = &world->component_stores[store_idx];
      const uint64_t comp_size = store->size;

      // Get the packed store that corresponds with this current store
      PackedComponentStore *packed_store = &packed_stores[store_idx];
      packed_store->id = store->id;
      packed_store->components =
          tb_alloc(tmp_alloc, store->size * store->count);
      packed_store->entity_ids =
          tb_alloc_nm_tp(tmp_alloc, store->count, EntityId);
      packed_store->count = 0;

      // Copy components from the core store to the packed store
      uint32_t packed_comp_idx = 0;
      for (uint32_t entity_id = 0; entity_id < world->max_entities;
           ++entity_id) {
        Entity entity = world->entities[entity_id];
        // If the entity is marked to use this component
        if ((entity & (1 << store_idx)) == 1) {
          uint8_t *component = &store->components[entity_id * comp_size];
          uint8_t *packed_comp_dst =
              &packed_store->components[packed_comp_idx * comp_size];
          SDL_memcpy(packed_comp_dst, component, comp_size);
          packed_store->entity_ids[packed_comp_idx] = (EntityId)entity_id;
          packed_comp_idx++;
          packed_store->count++;
        }
      }
    }

    TracyCZoneEnd(pack_ctx);
  }

  {
    TracyCZoneN(system_tick_ctx, "Tick Systems", true);
    TracyCZoneColor(system_tick_ctx, TracyCategoryColorCore);
    for (uint32_t system_idx = 0; system_idx < world->system_count;
         ++system_idx) {
      System *system = &world->systems[system_idx];

      // Gather packed component columns for this system
      const uint32_t set_count = system->dep_count >= MAX_DEPENDENCY_SET_COUT
                                     ? MAX_DEPENDENCY_SET_COUT
                                     : system->dep_count;

      SystemInput input = (SystemInput){
          .dep_set_count = set_count,
          .dep_sets = {0},
      };

      for (uint32_t i = 0; i < set_count; ++i) {
        const SystemComponentDependencies *deps = &system->deps[i];
        SystemDependencySet *set = &input.dep_sets[i];

        for (uint32_t column_idx = 0; column_idx < deps->count; ++column_idx) {
          const ComponentId comp_id = deps->dependent_ids[column_idx];
          for (uint32_t store_idx = 0; store_idx < world->component_store_count;
               ++store_idx) {
            if (packed_stores[store_idx].id == comp_id) {
              set->columns[set->column_count++] = &packed_stores[store_idx];
            }
          }
        }
      }

      // TODO: Construct output structure so that the system outputs to the
      // correct locations
      SystemOutput output = (SystemOutput){0};

      system->tick(system->self, &input, &output, delta_seconds);

      // HACK: Custom input handling on the world
      // To detect if the user issues a quit event
      if (system->id == InputSystemId) {
        uint32_t event_count = 0;
        if (tb_input_system_get_events(system->self, &event_count, NULL)) {
          if (event_count > 0) {
            SDL_Event *events =
                tb_alloc_nm_tp(tmp_alloc, event_count, SDL_Event);
            if (tb_input_system_get_events(system->self, &event_count,
                                           events)) {
              for (uint32_t i = 0; i < event_count; ++i) {
                const SDL_Event *e = &events[i];
                if (e->type == SDL_QUIT) {
                  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                              "World has encountered quit event and is "
                              "gracefully exiting.");
                  return false;
                  TracyCZoneEnd(system_tick_ctx);
                  TracyCZoneEnd(world_tick_ctx);
                }
              }
            }
          }
        }
      }
    }
    TracyCZoneEnd(system_tick_ctx);
  }

  TracyCZoneEnd(world_tick_ctx);
  return true;
}

void tb_destroy_world(World *world) {
  if (world->component_store_count > 0) {
    for (uint32_t i = 0; i < world->component_store_count; ++i) {
      ComponentStore *store = &world->component_stores[i];
      for (uint32_t comp_idx = 0; comp_idx < world->entity_count; ++comp_idx) {
        uint8_t *comp = &store->components[(store->size * comp_idx)];
        if (comp) {
          store->destroy(comp);
        }
      }
    }
    tb_free(world->std_alloc, world->component_stores);
  }

  if (world->system_count > 0) {
    for (uint32_t i = 0; i < world->system_count; ++i) {
      System *system = &world->systems[i];
      system->destroy(system->self);
      tb_free(world->std_alloc, system->self);
    }
    tb_free(world->std_alloc, world->systems);
  }

  *world = (World){0};
}

bool tb_world_load_scene(World *world, const char *scene_path) {
  Allocator std_alloc = world->std_alloc;
  Allocator tmp_alloc = world->tmp_alloc;

  // Get qualified path to scene asset
  char *asset_path = NULL;
  {
    const uint32_t max_asset_len = 2048;
    asset_path = tb_alloc(world->tmp_alloc, max_asset_len);
    SDL_memset(asset_path, 0, max_asset_len);
    SDL_snprintf(asset_path, max_asset_len, "%s%s", ASSET_PREFIX, scene_path);
  }
  TB_CHECK_RETURN(asset_path, "Failed to resolve asset path.", false);

  // Load glb off disk
  cgltf_data *data = NULL;
  {
    SDL_RWops *glb_file = SDL_RWFromFile(asset_path, "rb");
    TB_CHECK_RETURN(glb_file, "Failed to open glb.", false);

    cgltf_options options = {.type = cgltf_file_type_glb,
                             .memory =
                                 {
                                     .user_data = std_alloc.user_data,
                                     .alloc = std_alloc.alloc,
                                     .free = std_alloc.free,
                                 },
                             .file = {
                                 .read = sdl_read_glb,
                                 .release = sdl_release_glb,
                                 .user_data = glb_file,
                             }};

    cgltf_result res = cgltf_parse_file(&options, asset_path, &data);
    TB_CHECK_RETURN(res == cgltf_result_success, "Failed to parse glb.", false);

    res = cgltf_load_buffers(&options, data, asset_path);
    TB_CHECK_RETURN(res == cgltf_result_success, "Failed to load glb buffers.",
                    false);

#if !defined(FINAL)
    res = cgltf_validate(data);
    TB_CHECK_RETURN(res == cgltf_result_success, "Failed to validate glb.",
                    false);
#endif
  }
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  // Create an entity for each node
  for (cgltf_size i = 0; i < data->nodes_count; ++i) {
    const cgltf_node *node = &data->nodes[i];

    // Get component count
    uint32_t component_count = 0;
    {
      if (node->camera) {
        component_count++;
      }
      if (node->light) {
        component_count++;
      }
      if (node->mesh) {
      }
      if (node->skin) {
      }
      // Only nodes with a non-zero scale have transforms
      if (node->scale[0] != 0.0f && node->scale[1] != 0.0f &&
          node->scale[2] != 0.0f) {
        component_count++;
      }
    }

    ComponentId *component_ids =
        tb_alloc_nm_tp(tmp_alloc, component_count, ComponentId);
    InternalDescriptor *component_descriptors =
        tb_alloc_nm_tp(tmp_alloc, component_count, InternalDescriptor);

    EntityDescriptor entity_desc = {
        .component_count = component_count,
        .component_ids = component_ids,
        .component_descriptors = component_descriptors,
        .name = node->name, // Do we want to copy this onto some string pool?
    };

    uint32_t component_idx = 0;
    {
      {
        const cgltf_camera *camera = node->camera;
        if (camera) {
          const cgltf_camera_type type = camera->type;

          if (type == cgltf_camera_type_perspective) {
            // Add component to entity
            component_ids[component_idx] = CameraComponentId;
            component_descriptors[component_idx] = &camera->data.perspective;
            component_idx++;
          } else {
            // TODO: Handle ortho camera / invalid camera
            SDL_TriggerBreakpoint();
          }
        }
      }
      if (node->light) {
        const cgltf_light *light = node->light;
        if (light) {
          const cgltf_light_type type = light->type;
          if (type == cgltf_light_type_directional) {
            // Add component to entity
            component_ids[component_idx] = DirectionalLightComponentId;
            component_descriptors[component_idx] = light;
            component_idx++;
          } else {
            // TODO: Handle other light types
            SDL_TriggerBreakpoint();
          }
        }
      }
      if (node->mesh) {
      }
      if (node->skin) {
      }
      if (node->scale[0] != 0.0f && node->scale[1] != 0.0f &&
          node->scale[2] != 0.0f) {
        Transform transform = {0};
        {
          {
            transform.position[0] = node->translation[0];
            transform.position[1] = node->translation[1];
            transform.position[2] = node->translation[2];
          }
          {
            transform.rotation[0] = node->rotation[0];
            transform.rotation[1] = node->rotation[1];
            transform.rotation[2] = node->rotation[2];
          }
          {
            transform.scale[0] = node->scale[0];
            transform.scale[1] = node->scale[1];
            transform.scale[2] = node->scale[2];
          }
          // TODO: Children and hierarchy
        }

        TransformComponentDescriptor *transform_desc =
            tb_alloc_tp(tmp_alloc, TransformComponentDescriptor);
        transform_desc->transform = transform;

        // Add component to entity
        component_ids[component_idx] = TransformComponentId;
        component_descriptors[component_idx] = transform_desc;
        component_idx++;
      }
    }

    tb_world_add_entity(world, &entity_desc);
  }

  return true;
}

EntityId tb_world_add_entity(World *world, const EntityDescriptor *desc) {
  TracyCZoneN(ctx, "Add Entity", true);
  TracyCZoneColor(ctx, TracyCategoryColorCore);

  // Determine if we need to grow the entity list
  const uint32_t new_entity_count = world->entity_count + 1;
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
    for (uint32_t comp_idx = 0; comp_idx < desc->component_count; ++comp_idx) {
      if (desc->component_ids[comp_idx] == store->id) {
        // Mark this store as being used by the entity
        (*entity) |= (1 << store_idx);
        store->count++;

        // Create a component in the store at this entity index
        uint8_t *comp_head = &store->components[entity_id * store->size];
        if (!store->create(comp_head, desc->component_descriptors[comp_idx])) {
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

bool tb_world_remove_entity(World *world, EntityId id) {
  (void)world;
  (void)id;
  return false;
}
