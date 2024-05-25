#include "tb_mesh_system2.h"

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

// Describes the creation of a material that lives in a GLB file
typedef struct TbMeshGLTFLoadRequest {
  const char *path;
  const char *name;
} TbMeshGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbMeshGLTFLoadRequest);

ECS_TAG_DECLARE(TbMeshUploadable);
ECS_TAG_DECLARE(TbMeshLoaded);

// Systems

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
