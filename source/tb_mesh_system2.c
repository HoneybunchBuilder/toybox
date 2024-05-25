#include "tb_mesh_system2.h"

#include "tb_assets.h"
#include "tb_gltf.h"
#include "tb_task_scheduler.h"

// Internals

static const int32_t TbMaxParallelMeshLoads = 8;

typedef SDL_AtomicInt TbMeshQueueCounter;
ECS_COMPONENT_DECLARE(TbMeshQueueCounter);

typedef struct TbMeshCtx {
  VkDescriptorSetLayout set_layout;
  TbFrameDescriptorPoolList frame_set_pool;

  ecs_query_t *uploadable_mesh_query;
  ecs_query_t *loaded_mesh_query;
} TbMeshCtx;
ECS_COMPONENT_DECLARE(TbMeshCtx);

typedef struct TbMeshData {
  VkIndexType idx_type;
  TbHostBuffer host_buffer;
  TbBuffer gpu_buffer;
  VkBufferView index_view;
  VkBufferView attr_views[TB_INPUT_PERM_COUNT];
} TbMeshData;
ECS_COMPONENT_DECLARE(TbMeshData);

ECS_COMPONENT_DECLARE(TbMeshIndex);

// Describes the creation of a mesh that lives in a GLB file
typedef struct TbMeshGLTFLoadRequest {
  const char *path;
  const char *name;
} TbMeshGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbMeshGLTFLoadRequest);

ECS_TAG_DECLARE(TbMeshUploadable);
ECS_TAG_DECLARE(TbMeshLoaded);

typedef struct TbMeshLoadedArgs {
  ecs_world_t *ecs;
  TbMesh2 mesh;
  TbMeshData comp;
} TbMeshLoadedArgs;

void tb_mesh_loaded(const void *args) {
  tb_auto loaded_args = (const TbMeshLoadedArgs *)args;
  tb_auto ecs = loaded_args->ecs;
  tb_auto mesh = loaded_args->mesh;
  if (mesh == 0) {
    TB_CHECK(false, "Mesh load failed. Do we need to retry?");
  }

  ecs_add(ecs, mesh, TbMeshUploadable);
  ecs_set_ptr(ecs, mesh, TbMeshData, &loaded_args->comp);
}

typedef struct TbLoadCommonMeshArgs {
  ecs_world_t *ecs;
  TbMesh2 mesh;
  TbTaskScheduler enki;
  TbPinnedTask loaded_task;
} TbLoadCommonMeshArgs;

typedef struct TbLoadGLTFMeshArgs {
  TbLoadCommonMeshArgs common;
  TbMeshGLTFLoadRequest gltf;
} TbLoadGLTFMeshArgs;

void tb_load_gltf_mesh_task(const void *args) {
  TracyCZoneN(ctx, "Load GLTF Mesh Task", true);
  tb_auto load_args = (const TbLoadGLTFMeshArgs *)args;
  TbMesh2 mesh = load_args->common.mesh;

  tb_auto path = load_args->gltf.path;
  tb_auto name = load_args->gltf.name;

  tb_auto data = tb_read_glb(tb_thread_alloc, path);
  // Find mesh by name
  struct cgltf_mesh *gltf_mesh = NULL;
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    tb_auto m = &data->meshes[i];
    if (SDL_strcmp(name, m->name) == 0) {
      gltf_mesh = m;
      break;
    }
  }
  if (gltf_mesh == NULL) {
    TB_CHECK(false, "Failed to find mesh by name");
    mesh = 0; // Invalid ent means task failed
  }

  // Queue upload of mesh data to the GPU
  TbMeshData mesh_data = {0};
  if (mesh != 0) {
    TB_CHECK(false, "Test");
  }

  cgltf_free(data);

  tb_free(tb_global_alloc, (void *)path);
  tb_free(tb_global_alloc, (void *)name);

  // Launch pinned task to handle loading signals on main thread
  TbMeshLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .mesh = mesh,
      .comp = mesh_data,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbMeshLoadedArgs));
  TracyCZoneEnd(ctx);
}

// Systems

void tb_queue_gltf_mesh_loads(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Queue GLTF Mesh Loads", true);
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto counter = ecs_field(it, TbMeshQueueCounter, 2);
  tb_auto reqs = ecs_field(it, TbMeshGLTFLoadRequest, 3);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelMeshLoads) {
      break;
    }
    TbMesh2 ent = it->entities[i];
    tb_auto req = reqs[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_mesh_loaded, NULL, 0);

    TbLoadGLTFMeshArgs args = {
        .common =
            {
                .ecs = it->world,
                .mesh = ent,
                .enki = enki,
                .loaded_task = loaded_task,
            },
        .gltf = req,
    };
    TbTask load_task = tb_async_task(enki, tb_load_gltf_mesh_task, &args,
                                     sizeof(TbLoadGLTFMeshArgs));
    // Apply task component to texture entity
    ecs_set(it->world, ent, TbTask, {load_task});

    SDL_AtomicIncRef(counter);

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(it->world, ent, TbMeshGLTFLoadRequest);
  }

  TracyCZoneEnd(ctx);
}

void tb_reset_mesh_queue_count(ecs_iter_t *it) {
  tb_auto counter = ecs_field(it, TbMeshQueueCounter, 1);
  SDL_AtomicSet(counter, 0);
}

void tb_update_mesh_descriptors(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Update Mesh Descriptors", true);

  tb_auto mesh_ctx = ecs_field(it, TbMeshCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto world = ecs_singleton_get(it->world, TbWorldRef)->world;

  uint64_t mesh_count = 0;

  // Accumulate the number of meshes
  ecs_iter_t mesh_it = ecs_query_iter(it->world, mesh_ctx->loaded_mesh_query);
  while (ecs_query_next(&mesh_it)) {
    mesh_count += mesh_it.count;
  }

  if (mesh_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  const uint32_t view_count = TB_INPUT_PERM_COUNT + 1; // +1 for index buffer

  {
    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = view_count,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = mesh_count * view_count * 4,
                },
            },
    };
    VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = view_count,
        .pDescriptorCounts =
            (uint32_t[view_count]){mesh_count, mesh_count, mesh_count,
                                   mesh_count, mesh_count, mesh_count,
                                   mesh_count},
    };
    VkDescriptorSetLayout layouts[view_count] = {
        mesh_ctx->set_layout, mesh_ctx->set_layout, mesh_ctx->set_layout,
        mesh_ctx->set_layout, mesh_ctx->set_layout, mesh_ctx->set_layout,
        mesh_ctx->set_layout};
    tb_rnd_frame_desc_pool_tick(rnd_sys, &create_info, layouts, &alloc_info,
                                mesh_ctx->frame_set_pool.pools, view_count);
  }

  // Write all meshes into the descriptor set table
  uint32_t mesh_idx = 0;
  mesh_it = ecs_query_iter(it->world, mesh_ctx->loaded_mesh_query);

  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  TB_DYN_ARR_OF(VkBufferView) buf_views = {0};
  TB_DYN_ARR_RESET(writes, world->tmp_alloc, mesh_count * view_count);
  TB_DYN_ARR_RESET(buf_views, world->tmp_alloc, mesh_count * view_count);
  while (ecs_query_next(&mesh_it)) {
    tb_auto meshes = ecs_field(&mesh_it, TbMeshData, 1);
    for (int32_t i = 0; i < mesh_it.count; ++i) {
      tb_auto mesh = &meshes[i];

      // Index Write
      {
        TB_DYN_ARR_APPEND(buf_views, mesh->index_view);
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorCount = mesh_count,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .dstSet = tb_rnd_frame_desc_pool_get_set(
                rnd_sys, mesh_ctx->frame_set_pool.pools, 0),
            .dstBinding = 0,
            .pTexelBufferView = &TB_DYN_ARR_AT(buf_views, mesh_idx),
        };
        TB_DYN_ARR_APPEND(writes, write);
      }

      // Write per view
      for (uint32_t attr_idx = 0; attr_idx < TB_INPUT_PERM_COUNT; ++attr_idx) {
        TB_DYN_ARR_APPEND(buf_views, mesh->attr_views[attr_idx]);
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorCount = mesh_count,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .dstSet = tb_rnd_frame_desc_pool_get_set(
                rnd_sys, mesh_ctx->frame_set_pool.pools, attr_idx + 1),
            .dstBinding = 0,
            .pTexelBufferView = &TB_DYN_ARR_AT(buf_views, mesh_idx),
        };
        TB_DYN_ARR_APPEND(writes, write);
      }

      // Mesh is now ready to be referenced elsewhere
      ecs_set(it->world, mesh_it.entities[i], TbMeshIndex, {mesh_idx});
      mesh_idx++;
    }
  }
  tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);

  TracyCZoneEnd(ctx);
}

// Toybox Glue

void tb_register_mesh2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMeshCtx);
  ECS_COMPONENT_DEFINE(ecs, TbMeshData);
  ECS_COMPONENT_DEFINE(ecs, TbMeshQueueCounter);
  ECS_COMPONENT_DEFINE(ecs, TbMeshIndex);
  ECS_COMPONENT_DEFINE(ecs, TbMeshGLTFLoadRequest);
  ECS_TAG_DEFINE(ecs, TbMeshUploadable);
  ECS_TAG_DEFINE(ecs, TbMeshLoaded);

  ECS_SYSTEM(
      ecs, tb_queue_gltf_mesh_loads, EcsPreUpdate,
      TbTaskScheduler(TbTaskScheduler),
      TbMeshQueueCounter(TbMeshQueueCounter), [in] TbMeshGLTFLoadRequest);

  ECS_SYSTEM(ecs, tb_reset_mesh_queue_count,
             EcsPostUpdate, [in] TbMeshQueueCounter(TbMeshQueueCounter));
  ECS_SYSTEM(ecs, tb_update_mesh_descriptors, EcsPreStore,
             [in] TbMeshCtx(TbMeshCtx), [in] TbRenderSystem(TbRenderSystem));

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  TbMeshCtx ctx = {
      .uploadable_mesh_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbMeshData), .inout = EcsIn},
                        {.id = ecs_id(TbMeshUploadable), .inout = EcsIn},
                    }}),
      .loaded_mesh_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(TbMeshData), .inout = EcsIn},
                                  {.id = ecs_id(TbMeshLoaded)},
                              }}),
  };

  // Create mesh descriptor set layout
  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = 1,
                .pBindingFlags = (VkDescriptorBindingFlags[1]){flags},
            },
        .bindingCount = 1,
        .pBindings =
            (VkDescriptorSetLayoutBinding[1]){
                {
                    .binding = 0,
                    .descriptorCount = 4096,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                },
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Mesh Attr Layout",
                             &ctx.set_layout);
  }

  ecs_singleton_set_ptr(ecs, TbMeshCtx, &ctx);
}

void tb_unregister_mesh2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);

  ecs_query_fini(ctx->uploadable_mesh_query);
  ecs_query_fini(ctx->loaded_mesh_query);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);

  // TODO: Release all default references

  // TODO: Check for leaks

  // TODO: Clean up descriptor pool

  ecs_singleton_remove(ecs, TbMeshCtx);
}

TB_REGISTER_SYS(tb, mesh2, TB_MESH_SYS_PRIO)

// Public API

VkDescriptorSetLayout tb_mesh_sys_get_set_layout(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  return ctx->set_layout;
}

VkDescriptorSet tb_mesh_sys_get_pos_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_rnd_frame_desc_pool_get_set(rnd_sys, ctx->frame_set_pool.pools, 0);
}
VkDescriptorSet tb_mesh_sys_get_norm_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_rnd_frame_desc_pool_get_set(rnd_sys, ctx->frame_set_pool.pools, 1);
}
VkDescriptorSet tb_mesh_sys_get_tan_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_rnd_frame_desc_pool_get_set(rnd_sys, ctx->frame_set_pool.pools, 2);
}
VkDescriptorSet tb_mesh_sys_get_uv0_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_rnd_frame_desc_pool_get_set(rnd_sys, ctx->frame_set_pool.pools, 3);
}

TbMesh2 tb_mesh_sys_load_gltf_mesh(ecs_world_t *ecs, const char *path,
                                   const char *name) {
  // If an entity already exists with this name it is either loading or loaded
  TbMesh2 mesh_ent = ecs_lookup_child(ecs, ecs_id(TbMeshCtx), name);
  if (mesh_ent != 0) {
    return mesh_ent;
  }

  // Create a mesh entity
  mesh_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, mesh_ent, name);

  // It is a child of the mesh system context singleton
  ecs_add_pair(ecs, mesh_ent, EcsChildOf, ecs_id(TbMeshCtx));

  // Need to copy strings for task safety
  // Tasks are responsible for freeing these names
  const size_t path_len = SDL_strnlen(path, 256) + 1;
  char *path_cpy = tb_alloc_nm_tp(tb_global_alloc, path_len, char);
  SDL_strlcpy(path_cpy, path, path_len);

  const size_t name_len = SDL_strnlen(name, 256) + 1;
  char *name_cpy = tb_alloc_nm_tp(tb_global_alloc, name_len, char);
  SDL_strlcpy(name_cpy, name, name_len);

  // Append a mesh load request onto the entity to schedule loading
  ecs_set(ecs, mesh_ent, TbMeshGLTFLoadRequest, {path_cpy, name_cpy});

  return mesh_ent;
}

bool tb_is_mesh_ready(ecs_world_t *ecs, TbMesh2 mesh_ent) {
  return ecs_has(ecs, mesh_ent, TbMeshLoaded) &&
         ecs_has(ecs, mesh_ent, TbMeshIndex);
}
