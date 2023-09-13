#include "world.h"

#include "allocator.h"
#include "assets.h"
#include "assetsystem.h"
#include "profiling.h"
#include "simd.h"
#include "tbcommon.h"
#include "tbgltf.h"

#include "json-c/json_object.h"
#include "json-c/json_tokener.h"
#include "json-c/linkhash.h"

#include "cameracomponent.h"
#include "lightcomponent.h"
#include "meshcomponent.h"
#include "noclipcomponent.h"
#include "oceancomponent.h"
#include "skycomponent.h"
#include "transformcomponent.h"
#include "transformercomponents.h"

#include "audiosystem.h"
#include "camerasystem.h"
#include "coreuisystem.h"
#include "imguisystem.h"
#include "inputsystem.h"
#include "materialsystem.h"
#include "meshsystem.h"
#include "noclipcontrollersystem.h"
#include "oceansystem.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "rotatorsystem.h"
#include "shadowsystem.h"
#include "skysystem.h"
#include "texturesystem.h"
#include "timeofdaysystem.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"

#include <flecs.h>
#include <mimalloc.h>

#define tb_get_system_internal(systems, count, Type)                           \
  (Type *)tb_find_system_by_id(systems, count, Type##Id)->self;

void create_component_store(ComponentStore *store,
                            const ComponentDescriptor *desc) {
  store->name = desc->name;
  store->id = desc->id;
  store->id_str = desc->id_str;
  store->size = desc->size;
  store->desc_size = desc->desc_size;
  store->count = 0;
  store->components = NULL;
  store->desc = *desc;
  store->create = desc->create;
  store->deserialize = desc->deserialize;
  store->on_loaded = desc->on_loaded;
  store->destroy = desc->destroy;
}

bool create_system(World *world, System *system, const SystemDescriptor *desc) {
  system->name = desc->name;
  system->id = desc->id;
  system->create = desc->create;
  system->destroy = desc->destroy;

  // Allocate and initialize each system
  system->self = tb_alloc(world->std_alloc, desc->size);
  TB_CHECK_RETURN(system->self, "Failed to allocate system.", false);

  // If this system has dependencies on other systems, look those up now and
  // pass them to create
  uint32_t system_dep_count = desc->system_dep_count;
  TB_CHECK_RETURN(system_dep_count <= MAX_SYSTEM_DEP_COUNT,
                  "System dependency count out of range", false);
  for (uint32_t sys_dep_idx = 0; sys_dep_idx < system_dep_count;
       ++sys_dep_idx) {
    SystemId id = desc->system_deps[sys_dep_idx];
    System *sys = tb_find_system_by_id(world->systems, world->system_count, id);
    TB_CHECK_RETURN(sys,
                    "Failed to find dependent system, did you initialize "
                    "systems in the right order?",
                    false);
    system->system_deps[sys_dep_idx] = sys;
  }
  system->system_dep_count = system_dep_count;

  bool created = system->create(system->self, desc->desc,
                                system->system_dep_count, system->system_deps);
  TB_CHECK_RETURN(created, "Failed to create system internals.", false);

  return true;
}

uint32_t find_system_idx_by_id(const SystemDescriptor *descs,
                               uint32_t desc_count, SystemId id) {
  for (uint32_t i = 0; i < desc_count; ++i) {
    const SystemDescriptor *desc = &descs[i];
    if (desc->id == id) {
      return i;
    }
  }
  return SDL_MAX_UINT32;
}

int tick_desc_sort(const void *lhs, const void *rhs) {
  const TickFunctionDescriptor *a = (const TickFunctionDescriptor *)lhs;
  const TickFunctionDescriptor *b = (const TickFunctionDescriptor *)rhs;
  return a->order - b->order;
}

void *ecs_malloc(ecs_size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  void *ptr = mi_malloc(size);
  TracyCAllocN(ptr, size, "ECS");
  TracyCZoneEnd(ctx);
  return ptr;
}

void ecs_free(void *ptr) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(ptr, "ECS");
  mi_free(ptr);
  TracyCZoneEnd(ctx);
}

void *ecs_calloc(ecs_size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  void *ptr = mi_calloc(1, size);
  TracyCAllocN(ptr, size, "ECS");
  TracyCZoneEnd(ctx);
  return ptr;
}

void *ecs_realloc(void *original, ecs_size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(original, "ECS");
  void *ptr = mi_realloc(original, size);
  TracyCAllocN(ptr, size, "ECS");
  TracyCZoneEnd(ctx);
  return ptr;
}

TbWorld tb_create_world2(Allocator std_alloc, Allocator tmp_alloc,
                         RenderThread *render_thread, SDL_Window *window) {
  // Ensure the instrumented allocator is used
  ecs_os_set_api_defaults();
  ecs_os_api_t os_api = ecs_os_api;
  os_api.malloc_ = ecs_malloc;
  os_api.free_ = ecs_free;
  os_api.calloc_ = ecs_calloc;
  os_api.realloc_ = ecs_realloc;
  ecs_os_set_api(&os_api);

  TbWorld world = {
      .ecs = ecs_init(),
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
  };
  TB_DYN_ARR_RESET(world.scenes, std_alloc, 1);

  ecs_world_t *ecs = world.ecs;
  tb_register_audio_sys(&world);
  tb_register_render_sys(ecs, std_alloc, tmp_alloc, render_thread);
  tb_register_input_sys(ecs, tmp_alloc, window);
  tb_register_render_target_sys(ecs, std_alloc, tmp_alloc);
  tb_register_texture_sys(ecs, std_alloc, tmp_alloc);
  tb_register_view_sys(ecs, std_alloc, tmp_alloc);
  tb_register_render_object_sys(ecs, std_alloc, tmp_alloc);
  tb_register_render_pipeline_sys(ecs, std_alloc, tmp_alloc);
  tb_register_material_sys(ecs, std_alloc, tmp_alloc);
  tb_register_mesh_sys(ecs, std_alloc, tmp_alloc);
  tb_register_sky_sys(ecs, std_alloc, tmp_alloc);
  tb_register_imgui_sys(ecs, std_alloc, tmp_alloc);
  tb_register_noclip_sys(ecs, tmp_alloc);
  tb_register_core_ui_sys(ecs, std_alloc, tmp_alloc);
  tb_register_visual_logging_sys(ecs, std_alloc, tmp_alloc);
  // tb_register_ocean_sys(ecs, std_alloc, tmp_alloc);
  tb_register_camera_sys(ecs, std_alloc, tmp_alloc);
  // tb_register_shadow_sys(ecs, std_alloc, tmp_alloc);
  tb_register_time_of_day_sys(ecs);
  tb_register_rotator_sys(ecs, tmp_alloc);
  return world;
}

bool tb_tick_world2(TbWorld *world, float delta_seconds) {
  ecs_world_t *ecs = world->ecs;

  // Tick with flecs
  if (!ecs_progress(ecs, delta_seconds)) {
    return false;
  }
  // Manually check flecs for quit event
  ECS_COMPONENT(ecs, InputSystem);
  const InputSystem *in_sys = ecs_singleton_get(ecs, InputSystem);
  if (in_sys) {
    for (uint32_t event_idx = 0; event_idx < in_sys->event_count; ++event_idx) {
      if (in_sys->events[event_idx].type == SDL_QUIT) {
        return false;
      }
    }
  }
  return true;
}

void tb_clear_world2(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  TB_DYN_ARR_FOREACH(world->scenes, i) {
    TbScene *s = &TB_DYN_ARR_AT(world->scenes, i);
    tb_unload_scene2(world, s);
  }
  TB_DYN_ARR_CLEAR(world->scenes);
  // Progress ecs to process deletions
  ecs_progress(ecs, .1);
}

void tb_destroy_world2(TbWorld *world) {
  // Clean up singletons
  ecs_world_t *ecs = world->ecs;

  // Unregister systems so that they will be cleaned up by observers in ecs_fini
  tb_unregister_rotator_sys(ecs);

  tb_unregister_time_of_day_sys(ecs);
  tb_unregister_visual_logging_sys(ecs);
  tb_unregister_camera_sys(ecs);
  tb_unregister_sky_sys(ecs);
  tb_unregister_core_ui_sys(ecs);
  tb_unregister_imgui_sys(ecs);
  tb_unregister_mesh_sys(ecs);
  tb_unregister_material_sys(ecs);
  tb_unregister_render_pipeline_sys(ecs);
  tb_unregister_render_object_sys(ecs);
  tb_unregister_view_sys(ecs);
  tb_unregister_texture_sys(ecs);
  tb_unregister_render_target_sys(ecs);
  tb_unregister_render_sys(ecs);

  ecs_fini(ecs);
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
    world->system_count = system_count;

    System *systems = NULL;
    if (system_count > 0) {
      systems = tb_alloc_nm_tp(std_alloc, system_count, System);
      TB_CHECK_RETURN(systems, "Failed to allocate systems.", false);
      world->systems = systems;

      bool system_created = false;

      // Calculate the init order
      world->init_order =
          tb_alloc_nm_tp(std_alloc, desc->system_count, uint32_t);
      for (uint32_t i = 0; i < desc->system_count; ++i) {
        world->init_order[i] = find_system_idx_by_id(
            desc->system_descs, desc->system_count, desc->init_order[i]);
      }

      // Create in given init order
      for (uint32_t i = 0; i < desc->system_count; ++i) {
        uint32_t system_idx = world->init_order[i];
        const SystemDescriptor *sys_desc = &desc->system_descs[system_idx];

        system_created = create_system(world, &systems[system_idx], sys_desc);
        TB_CHECK_RETURN(system_created, "Failed to create system.", false);
      }

      // Tick V2
      // Gather a list of all tick functions from every system
      TB_DYN_ARR_OF(TickFunctionDescriptor) sorted_tick_descs;
      TB_DYN_ARR_RESET(sorted_tick_descs, std_alloc, 16);
      for (uint32_t sys_idx = 0; sys_idx < desc->system_count; ++sys_idx) {
        const SystemDescriptor *sys_desc = &desc->system_descs[sys_idx];
        for (uint32_t i = 0; i < sys_desc->tick_fn_count; ++i) {
          TB_DYN_ARR_APPEND(sorted_tick_descs, sys_desc->tick_fns[i]);
        }
      }
      if (!TB_DYN_ARR_EMPTY(sorted_tick_descs)) {
        // Sort the list of tick funcions
        SDL_qsort(sorted_tick_descs.data, TB_DYN_ARR_SIZE(sorted_tick_descs),
                  sizeof(TickFunctionDescriptor), tick_desc_sort);
      }

      // Create a tick function for each now sorted descriptor
      world->tick_fn_count = TB_DYN_ARR_SIZE(sorted_tick_descs);
      world->tick_functions =
          tb_alloc_nm_tp(std_alloc, world->tick_fn_count, TickFunction);
      TB_DYN_ARR_FOREACH(sorted_tick_descs, i) {
        const TickFunctionDescriptor *fn_desc =
            &TB_DYN_ARR_AT(sorted_tick_descs, i);
        TickFunction *fn = &world->tick_functions[i];
        *fn = (TickFunction){
            .dep_count = fn_desc->dep_count,
            .system = tb_find_system_by_id(world->systems, world->system_count,
                                           fn_desc->system_id),
            .function = fn_desc->function,
        };
        SDL_memcpy(fn->deps, fn_desc->deps,
                   fn_desc->dep_count * sizeof(SystemComponentDependencies));
      }

      TB_DYN_ARR_DESTROY(sorted_tick_descs);
    }
  }

  return true;
}

bool tb_tick_world(World *world, float delta_seconds) {
  TracyCZoneN(world_tick_ctx, "World Tick", true);
  TracyCZoneColor(world_tick_ctx, TracyCategoryColorCore);

  Allocator tmp_alloc = world->tmp_alloc;

  {
    TracyCZoneN(system_tick_ctx, "Tick Systems", true);
    TracyCZoneColor(system_tick_ctx, TracyCategoryColorCore);

    {
      // Tick functions should already be in the correct order
      for (uint32_t tick_idx = 0; tick_idx < world->tick_fn_count; ++tick_idx) {
        const TickFunction *tick_fn = &world->tick_functions[tick_idx];

        // Gather and pack component columns for this system's input
        const uint32_t set_count = tick_fn->dep_count;

        SystemInput input = (SystemInput){
            .dep_set_count = set_count,
            .dep_sets = {{0}},
        };

        // Each dependency set can have a number of dependent columns
        for (uint32_t set_idx = 0; set_idx < set_count; ++set_idx) {
          // Gather the components that this dependency set requires
          const SystemComponentDependencies *dep = &tick_fn->deps[set_idx];
          SystemDependencySet *set = &input.dep_sets[set_idx];

          // Find the entities that have the required components
          uint32_t entity_count = 0;
          for (EntityId entity_id = 0; entity_id < world->entity_count;
               ++entity_id) {
            Entity entity = world->entities[entity_id];
            uint32_t matched_component_count = 0;
            for (uint32_t store_idx = 0;
                 store_idx < world->component_store_count; ++store_idx) {
              const ComponentId store_id =
                  world->component_stores[store_idx].id;
              for (uint32_t i = 0; i < dep->count; ++i) {
                if (store_id == dep->dependent_ids[i]) {
                  if (entity & (1 << store_idx)) {
                    matched_component_count++;
                  }
                  break;
                }
              }
            }
            // Count if this entity has all required components
            if (matched_component_count == dep->count) {
              entity_count++;
            }
          }

          // Allocate collection of entities
          EntityId *entities =
              tb_alloc_nm_tp(tmp_alloc, entity_count, EntityId);
          entity_count = 0;
          for (EntityId entity_id = 0; entity_id < world->entity_count;
               ++entity_id) {
            Entity entity = world->entities[entity_id];
            uint32_t matched_component_count = 0;
            for (uint32_t store_idx = 0;
                 store_idx < world->component_store_count; ++store_idx) {
              const ComponentId store_id =
                  world->component_stores[store_idx].id;
              for (uint32_t i = 0; i < dep->count; ++i) {
                if (store_id == dep->dependent_ids[i]) {
                  if (entity & (1 << store_idx)) {
                    matched_component_count++;
                  }
                  break;
                }
              }
            }
            // Count if this entity has all required components
            if (matched_component_count == dep->count) {
              entities[entity_count] = entity_id;
              entity_count++;
            }
          }
          set->entity_count = entity_count;
          set->entity_ids = entities;

          // Now we know how much we need to allocate for eached packed
          // component store
          set->column_count = dep->count;
          for (uint32_t col_id = 0; col_id < dep->count; ++col_id) {
            const ComponentId id = dep->dependent_ids[col_id];

            uint64_t components_size = 0;
            const ComponentStore *world_store = NULL;
            // Find world component store for this id and determine how much
            // space we need for the packed component store
            for (uint32_t comp_idx = 0; comp_idx < world->component_store_count;
                 ++comp_idx) {
              const ComponentStore *store = &world->component_stores[comp_idx];
              if (store->id == id) {
                world_store = store;
                components_size = entity_count * store->size;
                break;
              }
            }
            if (components_size > 0) {
              uint8_t *components =
                  tb_alloc_nm_tp(tmp_alloc, components_size, uint8_t);
              const uint64_t comp_size = world_store->size;

              // Copy from the world store based on entity index into the packed
              // store
              for (uint32_t entity_idx = 0; entity_idx < entity_count;
                   ++entity_idx) {
                EntityId entity_id = entities[entity_idx];

                const uint8_t *in_comp =
                    &world_store->components[entity_id * comp_size];
                uint8_t *out_comp = &components[entity_idx * comp_size];

                SDL_memcpy(out_comp, in_comp, comp_size);
              }

              set->columns[col_id] = (PackedComponentStore){
                  .id = id,
                  .components = components,
              };
            }
          }
        }

        SystemOutput output = (SystemOutput){0};
        tick_fn->function(tick_fn->system->self, &input, &output,
                          delta_seconds);

        // Write output back to world stores
        TB_CHECK(output.set_count <= MAX_OUTPUT_SET_COUNT,
                 "Too many output sets");
        for (uint32_t set_idx = 0; set_idx < output.set_count; ++set_idx) {
          const SystemWriteSet *set = &output.write_sets[set_idx];

          // Get the world component store that matches this set's component id
          ComponentStore *comp_store = NULL;
          for (uint32_t store_idx = 0; store_idx < world->component_store_count;
               ++store_idx) {
            ComponentStore *store = &world->component_stores[store_idx];
            if (set->id == store->id) {
              comp_store = store;
              break;
            }
          }
          TB_CHECK(comp_store, "Failed to retrieve component store");

          // Write out the components from the output set
          for (uint32_t entity_idx = 0; entity_idx < set->count; ++entity_idx) {
            EntityId entity_id = set->entities[entity_idx];

            const uint64_t comp_size = comp_store->size;

            const uint8_t *src = &set->components[entity_idx * comp_size];
            uint8_t *dst = &comp_store->components[entity_id * comp_size];

            SDL_memcpy(dst, src, comp_size);
          }
        }
      }
    }

    // Check for quit event
    {
      InputSystem *in_sys = tb_get_system_internal(
          world->systems, world->system_count, InputSystem);
      if (in_sys) {
        for (uint32_t event_idx = 0; event_idx < in_sys->event_count;
             ++event_idx) {
          if (in_sys->events[event_idx].type == SDL_QUIT) {
            TracyCZoneEnd(system_tick_ctx);
            TracyCZoneEnd(world_tick_ctx);
            return false;
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
  tb_world_unload_scene(world);

  if (world->system_count > 0) {
    // Shutdown in reverse init order
    for (int32_t i = world->system_count - 1; i >= 0; --i) {
      const uint32_t idx = world->init_order[i];
      System *system = &world->systems[idx];
      system->destroy(system->self);
      tb_free(world->std_alloc, system->self);
    }
    tb_free(world->std_alloc, world->systems);
  }
  tb_free(world->std_alloc, world->tick_functions);

  *world = (World){0};
}

EntityId load_entity(World *world, json_tokener *tok, const cgltf_data *data,
                     const char *root_scene_path, EntityId parent,
                     const cgltf_node *node) {
  // Get extras
  cgltf_size extra_size = 0;
  char *extra_json = NULL;
  if (node->extras.end_offset != 0 && node->extras.start_offset != 0) {
    extra_size = (node->extras.end_offset - node->extras.start_offset) + 1;
    extra_json = tb_alloc_nm_tp(world->tmp_alloc, extra_size, char);
    if (cgltf_copy_extras_json(data, &node->extras, extra_json, &extra_size) !=
        cgltf_result_success) {
      extra_size = 0;
      extra_json = NULL;
    }
  }

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
      component_count++;
    }
    if (node->skin) {
    }
    // Only nodes with a non-zero scale have transforms
    if (node->scale[0] != 0.0f && node->scale[1] != 0.0f &&
        node->scale[2] != 0.0f) {
      component_count++;
    }
    // Find custom components
    if (extra_json) {
      json_object *json =
          json_tokener_parse_ex(tok, extra_json, (int32_t)extra_size);
      if (json) {
        json_object_object_foreach(json, key, value) {
          if (SDL_strncmp(key, "id", 2) == 0) {
            const char *id_str = json_object_get_string(value);

            for (uint32_t comp_idx = 0; comp_idx < world->component_store_count;
                 ++comp_idx) {
              const ComponentStore *store = &world->component_stores[comp_idx];
              if (store->deserialize &&
                  SDL_strcmp(store->id_str, id_str) == 0) {
                component_count++;
                break;
              }
            }
          }
        }
      }
    }
  }

  ComponentId *component_ids =
      tb_alloc_nm_tp(world->tmp_alloc, component_count, ComponentId);
  InternalDescriptor *component_descriptors =
      tb_alloc_nm_tp(world->tmp_alloc, component_count, InternalDescriptor);

  EntityDescriptor entity_desc = {
      .component_count = component_count,
      .component_ids = component_ids,
      .component_descriptors = component_descriptors,
      .name = node->name, // Do we want to copy this onto some string pool?
  };

  uint32_t component_idx = 0;
  {
    if (node->camera) {
      cgltf_camera *camera = tb_alloc_tp(world->tmp_alloc, cgltf_camera);
      if (camera) {
        SDL_memcpy(camera, node->camera, sizeof(cgltf_camera));
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
      cgltf_light *light = tb_alloc_tp(world->tmp_alloc, cgltf_light);
      if (light) {
        SDL_memcpy(light, node->light, sizeof(cgltf_light));
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
      MeshComponentDescriptor *mesh_desc =
          tb_alloc_tp(world->tmp_alloc, MeshComponentDescriptor);
      *mesh_desc = (MeshComponentDescriptor){
          .node = node,
          .source_path = root_scene_path,
      };

      // Add component to entity
      component_ids[component_idx] = MeshComponentId;
      component_descriptors[component_idx] = mesh_desc;
      component_idx++;
    }
    if (node->skin) {
    }
    if (node->scale[0] != 0.0f || node->scale[1] != 0.0f ||
        node->scale[2] != 0.0f) {
      Transform transform = tb_transform_from_node(node);

      TransformComponentDescriptor *transform_desc =
          tb_alloc_tp(world->tmp_alloc, TransformComponentDescriptor);
      transform_desc->transform = transform;
      transform_desc->parent = parent;
      transform_desc->world = world;

      // Add component to entity
      component_ids[component_idx] = TransformComponentId;
      component_descriptors[component_idx] = transform_desc;
      component_idx++;
    }

    // Add extra components to entity
    if (extra_json) {
      json_object *json =
          json_tokener_parse_ex(tok, extra_json, (int32_t)extra_size);
      if (json) {
        json_object_object_foreach(json, key, value) {
          if (SDL_strcmp(key, "id") == 0) {
            const char *id_str = json_object_get_string(value);

            for (uint32_t comp_idx = 0; comp_idx < world->component_store_count;
                 ++comp_idx) {
              const ComponentStore *store = &world->component_stores[comp_idx];
              if (store->deserialize &&
                  SDL_strcmp(store->id_str, id_str) == 0) {
                void *comp_desc = tb_alloc(world->tmp_alloc, store->desc_size);
                SDL_memset(comp_desc, 0, store->desc_size);
                store->deserialize(json, comp_desc);
                component_ids[component_idx] = store->id;
                component_descriptors[component_idx] = comp_desc;
                component_idx++;
                break;
              }
            }
          }
        }
      }
    }
  }
  EntityId id = tb_world_add_entity(world, &entity_desc);

  if (node->children_count > 0) {
    // Get the transform store and find this entity's transform component
    ComponentStore *transform_store = NULL;
    uint32_t transform_idx = 0;
    for (uint32_t i = 0; i < world->component_store_count; ++i) {
      if (world->component_stores[i].id == TransformComponentId) {
        transform_store = &world->component_stores[i];
        transform_idx = i;
      }
    }
    TB_CHECK(transform_store, "Unexepcted");

    // Make sure this entity actually has a transform
    if (world->entities[id] & (1 << transform_idx)) {
      TransformComponent *trans_comp = (TransformComponent *)tb_get_component(
          transform_store, id, TransformComponent);
      trans_comp->child_count = node->children_count;
      trans_comp->children =
          tb_alloc_nm_tp(world->std_alloc, node->children_count, EntityId);
      EntityId *children = trans_comp->children;

      // Load all children
      for (uint32_t i = 0; i < node->children_count; ++i) {
        const cgltf_node *child = node->children[i];
        EntityId child_id =
            load_entity(world, tok, data, root_scene_path, id, child);
        children[i] = child_id;
      }
    }
  }

  return id;
}

ecs_entity_t load_entity2(ecs_world_t *ecs, Allocator std_alloc,
                          Allocator tmp_alloc, json_tokener *tok,
                          const cgltf_data *data, const char *root_scene_path,
                          ecs_entity_t parent, const cgltf_node *node) {
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, AssetSystem);
  // Get extras
  cgltf_size extra_size = 0;
  char *extra_json = NULL;
  if (node->extras.end_offset != 0 && node->extras.start_offset != 0) {
    extra_size = (node->extras.end_offset - node->extras.start_offset) + 1;
    extra_json = tb_alloc_nm_tp(tmp_alloc, extra_size, char);
    if (cgltf_copy_extras_json(data, &node->extras, extra_json, &extra_size) !=
        cgltf_result_success) {
      extra_size = 0;
      extra_json = NULL;
    }
  }

  // Create an entity
  ecs_entity_t e = ecs_new(ecs, 0);

  // Attempt to add a component for each asset system provided
  ecs_filter_t *asset_filter = ecs_filter(ecs, {.terms = {
                                                    {ecs_id(AssetSystem)},
                                                }});
  ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
  while (ecs_filter_next(&asset_it)) {
    AssetSystem *asset_sys = ecs_field(&asset_it, AssetSystem, 1);
    for (int32_t i = 0; i < asset_it.count; ++i) {
      if (!asset_sys[i].add_fn(ecs, e, root_scene_path, node, extra_json)) {
        TB_CHECK_RETURN(false, "Failed to handle component parsing", 0);
      }
    }
  }
  ecs_filter_fini(asset_filter);

  if (node->children_count > 0) {
    TransformComponent *trans_comp = ecs_get_mut(ecs, e, TransformComponent);
    // Make sure this entity actually has a transform
    if (trans_comp) {
      trans_comp->child_count = node->children_count;
      trans_comp->children =
          tb_alloc_nm_tp(std_alloc, node->children_count, EntityId);
      EntityId *children = trans_comp->children;

      // Load all children
      for (uint32_t i = 0; i < node->children_count; ++i) {
        const cgltf_node *child = node->children[i];
        EntityId child_id = load_entity2(ecs, std_alloc, tmp_alloc, tok, data,
                                         root_scene_path, e, child);
        children[i] = child_id;
      }
      ecs_modified(ecs, e, TransformComponent);
    }
  }

  return e;
}

bool tb_world_load_scene(World *world, const char *scene_path) {
  Allocator std_alloc = world->std_alloc;
  Allocator tmp_alloc = world->tmp_alloc;

  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(tmp_alloc, scene_path);

  // Load glb off disk
  cgltf_data *data = tb_read_glb(std_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  json_tokener *tok = json_tokener_new();

  uint32_t entity_tail = world->entity_count;

  // Create an entity for each node
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
    const cgltf_node *node = data->scene->nodes[i];
    load_entity(world, tok, data, scene_path, InvalidEntityId, node);
  }

  json_tokener_free(tok);

  {
    TracyCZoneN(ctx, "On Loaded", true);

    for (uint32_t store_idx = 0; store_idx < world->component_store_count;
         ++store_idx) {
      ComponentStore *store = &world->component_stores[store_idx];
      if (store->on_loaded) {
        // Only bother with new entities
        for (uint32_t id = entity_tail; id < world->entity_count; ++id) {
          Entity ent = world->entities[id];
          if (ent & (1 << store_idx)) {
            store->on_loaded(id, world,
                             (void *)&store->components[id * store->size]);
          }
        }
      }
    }

    TracyCZoneEnd(ctx);
  }

  // Clean up gltf file now that it's parsed
  cgltf_free(data);

  return true;
}

void tb_world_unload_scene(World *world) {
  if (world->component_store_count > 0) {
    for (uint32_t store_idx = 0; store_idx < world->component_store_count;
         ++store_idx) {
      ComponentStore *store = &world->component_stores[store_idx];
      for (EntityId entity_id = 0; entity_id < world->entity_count;
           ++entity_id) {
        Entity entity = world->entities[entity_id];
        uint8_t *comp = &store->components[(store->size * entity_id)];
        if (entity & (1 << store_idx)) {
          uint32_t system_dep_count = 0;
          System **system_deps = NULL;

          const ComponentDescriptor *comp_desc = &store->desc;

          if (comp_desc) {
            // Find system dependencies
            system_dep_count = comp_desc->system_dep_count;
            if (system_dep_count > 0) {
              system_deps =
                  tb_alloc_nm_tp(world->tmp_alloc, system_dep_count, System *);
              for (uint32_t dep_idx = 0; dep_idx < system_dep_count;
                   ++dep_idx) {
                for (uint32_t sys_idx = 0; sys_idx < world->system_count;
                     ++sys_idx) {
                  if (world->systems[sys_idx].id ==
                      comp_desc->system_deps[dep_idx]) {
                    system_deps[dep_idx] = &world->systems[sys_idx];
                    break;
                  }
                }
              }
            }
          }

          store->destroy(comp, system_dep_count, system_deps);
        }
      }
    }
    tb_free(world->std_alloc, world->component_stores);
  }

  // Clear out entities
  for (uint32_t i = 0; i < world->entity_count; ++i) {
    world->entities[i] = 0;
  }
  world->entity_count = 0;
}

bool tb_load_scene2(TbWorld *world, const char *scene_path) {
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(world->tmp_alloc, scene_path);

  // Load glb off disk
  cgltf_data *data = tb_read_glb(world->std_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  json_tokener *tok = json_tokener_new();

  TbScene scene = {0};
  TB_DYN_ARR_RESET(scene.entities, world->std_alloc, data->scene->nodes_count);

  // Create an entity for each node
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
    const cgltf_node *node = data->scene->nodes[i];
    ecs_entity_t e =
        load_entity2(world->ecs, world->std_alloc, world->tmp_alloc, tok, data,
                     scene_path, 0, node);
    TB_DYN_ARR_APPEND(scene.entities, e);
  }

  TB_DYN_ARR_APPEND(world->scenes, scene);

  json_tokener_free(tok);

  // Clean up gltf file now that it's parsed
  cgltf_free(data);

  return true;
}

void tb_unload_scene2(TbWorld *world, TbScene *scene) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, AssetSystem);

  // Remove all components managed by the asset system from the scene
  // TODO: This doesn't handle the case of multiple scenes
  ecs_filter_t *asset_filter = ecs_filter(ecs, {.terms = {
                                                    {ecs_id(AssetSystem)},
                                                }});
  ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
  while (ecs_filter_next(&asset_it)) {
    AssetSystem *asset_sys = ecs_field(&asset_it, AssetSystem, 1);
    for (int32_t i = 0; i < asset_it.count; ++i) {
      asset_sys[i].rem_fn(ecs);
    }
  }
  ecs_filter_fini(asset_filter);

  // Remove all entities in the scene from the world
  TB_DYN_ARR_FOREACH(scene->entities, i) {
    ecs_delete(world->ecs, TB_DYN_ARR_AT(scene->entities, i));
  }
}

EntityId tb_world_add_entity(World *world, const EntityDescriptor *desc) {
  TracyCZoneNC(ctx, "Add Entity", TracyCategoryColorCore, true);
  // Determine if we need to grow the entity list
  {
    TracyCZoneNC(resize_ctx, "Add To Collection", TracyCategoryColorCore, true);
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
    TracyCZoneEnd(resize_ctx);
  }
  EntityId entity_id = world->entity_count;
  Entity *entity = &world->entities[entity_id];
  *entity = 0; // Must initialize the entity
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

        uint32_t system_dep_count = 0;
        System **system_deps = NULL;

        const ComponentDescriptor *comp_desc = &store->desc;

        if (comp_desc) {
          // Find system dependencies
          TracyCZoneNC(sys_dep_ctx, "Find Sys Deps", TracyCategoryColorCore,
                       true);
          system_dep_count = comp_desc->system_dep_count;
          if (system_dep_count > 0) {
            system_deps =
                tb_alloc_nm_tp(world->tmp_alloc, system_dep_count, System *);
            for (uint32_t dep_idx = 0; dep_idx < system_dep_count; ++dep_idx) {
              for (uint32_t sys_idx = 0; sys_idx < world->system_count;
                   ++sys_idx) {
                if (world->systems[sys_idx].id ==
                    comp_desc->system_deps[dep_idx]) {
                  system_deps[dep_idx] = &world->systems[sys_idx];
                  break;
                }
              }
            }
          }
          TracyCZoneEnd(sys_dep_ctx);
        }

        // Create a component in the store at this entity index
        TracyCZoneNC(comp_ctx, "Create Component", TracyCategoryColorCore,
                     true);
        uint8_t *comp_head = &store->components[entity_id * store->size];
        if (!store->create(comp_head, desc->component_descriptors[comp_idx],
                           system_dep_count, system_deps)) {
          SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "%s",
                       "Failed to create component for entity.");
          SDL_TriggerBreakpoint();
          TracyCZoneEnd(comp_ctx);
          TracyCZoneEnd(ctx);
          return (EntityId)-1;
        }
        TracyCZoneEnd(comp_ctx);

        break;
      }
    }
  }

  TracyCZoneEnd(ctx);
  return entity_id;
}

const PackedComponentStore *tb_get_column_check_id(const SystemInput *input,
                                                   uint32_t set, uint32_t index,
                                                   ComponentId id) {
  TracyCZoneNC(ctx, "get column check id", TracyCategoryColorCore, true);
  TB_CHECK_RETURN(set < MAX_DEPENDENCY_SET_COUNT,
                  "Dependency set index out of range", NULL);
  TB_CHECK_RETURN(index < MAX_DEPENDENCY_SET_COUNT,
                  "Component Store index out of range", NULL);
  if (set >= input->dep_set_count) {
    TracyCZoneEnd(ctx);
    return NULL;
  }

  const SystemDependencySet *dep = &input->dep_sets[set];
  if (index >= dep->column_count) {
    TracyCZoneEnd(ctx);
    return NULL;
  }

  // Make sure it's the id the caller wanted
  const PackedComponentStore *store = &dep->columns[index];
  if (store->id == id) {
    TracyCZoneEnd(ctx);
    return store;
  }

  TracyCZoneEnd(ctx);
  return NULL;
}

uint32_t tb_get_column_component_count(const SystemInput *input, uint32_t set) {
  TB_CHECK_RETURN(set < MAX_DEPENDENCY_SET_COUNT,
                  "Dependency set index out of range", SDL_MAX_UINT32);
  TB_CHECK_RETURN(set < input->dep_set_count, "Set out of range",
                  SDL_MAX_UINT32);

  const SystemDependencySet *dep = &input->dep_sets[set];
  return dep->entity_count;
}

EntityId *tb_get_column_entity_ids(const SystemInput *input, uint32_t set) {
  TB_CHECK_RETURN(set < MAX_DEPENDENCY_SET_COUNT,
                  "Dependency set index out of range", NULL);
  TB_CHECK_RETURN(set < input->dep_set_count, "Set out of range", NULL);

  const SystemDependencySet *dep = &input->dep_sets[set];
  return dep->entity_ids;
}

System *tb_find_system_by_id(System *systems, uint32_t system_count,
                             SystemId id) {
  TracyCZoneNC(ctx, "Find System By Id", TracyCategoryColorCore, true);
  for (uint32_t i = 0; i < system_count; ++i) {
    System *system = &systems[i];
    if (system->id == id) {
      TracyCZoneEnd(ctx);
      return system;
    }
  }
  TracyCZoneEnd(ctx);
  return NULL;
}

System *tb_find_system_dep_by_id(System *const *systems, uint32_t system_count,
                                 SystemId id) {
  TracyCZoneNC(ctx, "Find System By Dependency Id", TracyCategoryColorCore,
               true);
  for (uint32_t i = 0; i < system_count; ++i) {
    System *system = systems[i];
    if (system->id == id) {
      TracyCZoneEnd(ctx);
      return system;
    }
  }
  TracyCZoneEnd(ctx);
  return NULL;
}
void *tb_find_system_dep_self_by_id(System *const *systems,
                                    uint32_t system_count, SystemId id) {
  System *sys = tb_find_system_dep_by_id(systems, system_count, id);
  if (sys) {
    return sys->self;
  }
  return NULL;
}
