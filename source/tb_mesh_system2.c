#include "tb_mesh_system2.h"

#include "tb_assets.h"
#include "tb_gltf.h"
#include "tb_log.h"
#include "tb_material_system.h"
#include "tb_task_scheduler.h"
#include "tb_util.h"

// Internals

// Mesh system probably shouldn't own this
ECS_COMPONENT_DECLARE(TbAABB);

static const int32_t TbMaxParallelMeshLoads = 8;

ECS_TAG_DECLARE(TbMeshLoadPhaseLoading);
ECS_TAG_DECLARE(TbMeshLoadPhaseLoaded);
ECS_TAG_DECLARE(TbMeshLoadPhaseWriting);
ECS_TAG_DECLARE(TbMeshLoadPhaseWritten);
ECS_TAG_DECLARE(TbMeshLoadPhaseReady);

typedef SDL_AtomicInt TbMeshQueueCounter;
ECS_COMPONENT_DECLARE(TbMeshQueueCounter);
typedef SDL_AtomicInt TbSubMeshQueueCounter;
ECS_COMPONENT_DECLARE(TbSubMeshQueueCounter);

ECS_COMPONENT_DECLARE(TbSubMesh2Data);

typedef struct TbMeshCtx {
  VkDescriptorSetLayout set_layout;
  TbFrameDescriptorPoolList frame_set_pool;

  uint32_t owned_mesh_count;
  uint32_t pool_update_counter;

  ecs_query_t *loaded_mesh_query;
  ecs_query_t *dirty_mesh_query;
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
  uint32_t index;
} TbMeshGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbMeshGLTFLoadRequest);

typedef struct TbSubMeshGLTFLoadRequest {
  const char *path;
  const cgltf_mesh *gltf_mesh;
} TbSubMeshGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbSubMeshGLTFLoadRequest);

ECS_TAG_DECLARE(TbMeshLoaded);
ECS_TAG_DECLARE(TbMeshParsed);
ECS_TAG_DECLARE(TbMeshReady);
ECS_TAG_DECLARE(TbSubMeshParsed);
ECS_TAG_DECLARE(TbSubMeshReady);

typedef struct TbMeshLoadedArgs {
  ecs_world_t *ecs;
  TbMesh2 mesh;
  TbMeshData comp;
  const char *source_path;
  const cgltf_mesh *gltf_mesh;
} TbMeshLoadedArgs;

void tb_mesh_loaded(const void *args) {
  TracyCZoneN(ctx, "Mesh Loaded", true);
  tb_auto loaded_args = (const TbMeshLoadedArgs *)args;
  tb_auto ecs = loaded_args->ecs;
  tb_auto mesh = loaded_args->mesh;
  tb_auto source_path = loaded_args->source_path;
  tb_auto gltf_mesh = loaded_args->gltf_mesh;
  if (mesh == 0) {
    TB_CHECK(false, "Mesh load failed. Do we need to retry?");
    TracyCZoneEnd(ctx);
  }

  TbSubMeshGLTFLoadRequest submesh_req = {
      .path = source_path,
      .gltf_mesh = gltf_mesh,
  };

  ecs_add(ecs, mesh, TbMeshLoaded);
  ecs_add(ecs, mesh, TbMeshParsed);
  ecs_set_ptr(ecs, mesh, TbMeshData, &loaded_args->comp);
  ecs_set_ptr(ecs, mesh, TbSubMeshGLTFLoadRequest, &submesh_req);

  TracyCZoneEnd(ctx);
}

typedef struct TbLoadCommonMeshArgs {
  ecs_world_t *ecs;
  TbRenderSystem *rnd_sys;
  TbMesh2 mesh;
  TbTaskScheduler enki;
  TbPinnedTask loaded_task;
} TbLoadCommonMeshArgs;

typedef struct TbLoadGLTFMeshArgs {
  TbLoadCommonMeshArgs common;
  TbMeshGLTFLoadRequest gltf;
} TbLoadGLTFMeshArgs;

TbMeshData tb_load_gltf_mesh(TbRenderSystem *rnd_sys,
                             const cgltf_mesh *gltf_mesh) {
  TracyCZoneN(ctx, "Load GLTF Mesh", true);
  TbMeshData data = {0};

  // Determine how big this mesh is
  uint64_t index_size = 0;
  uint64_t geom_size = 0;
  uint64_t attr_size_per_type[cgltf_attribute_type_max_enum] = {0};
  {
    // Determine mesh index type
    {
      tb_auto stride = gltf_mesh->primitives[0].indices->stride;
      if (stride == sizeof(uint16_t)) {
        data.idx_type = VK_INDEX_TYPE_UINT16;
      } else if (stride == sizeof(uint32_t)) {
        data.idx_type = VK_INDEX_TYPE_UINT32;
      } else {
        TB_CHECK(false, "Unexpected index stride");
      }
    }

    uint64_t vertex_size = 0;
    uint32_t vertex_count = 0;
    for (cgltf_size prim_idx = 0; prim_idx < gltf_mesh->primitives_count;
         ++prim_idx) {
      cgltf_primitive *prim = &gltf_mesh->primitives[prim_idx];
      cgltf_accessor *indices = prim->indices;
      cgltf_size idx_size =
          tb_calc_aligned_size(indices->count, indices->stride, 16);

      index_size += idx_size;
      vertex_count = prim->attributes[0].data->count;

      for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
           ++attr_idx) {
        // Only care about certain attributes at the moment
        cgltf_attribute_type type = prim->attributes[attr_idx].type;
        int32_t idx = prim->attributes[attr_idx].index;
        if ((type == cgltf_attribute_type_position ||
             type == cgltf_attribute_type_normal ||
             type == cgltf_attribute_type_tangent ||
             type == cgltf_attribute_type_texcoord) &&
            idx == 0) {
          cgltf_accessor *attr = prim->attributes[attr_idx].data;
          uint64_t attr_size = vertex_count * attr->stride;
          attr_size_per_type[type] += attr_size;
        }
      }

      for (uint32_t i = 0; i < cgltf_attribute_type_max_enum; ++i) {
        tb_auto attr_size = attr_size_per_type[i];
        if (attr_size > 0) {
          attr_size_per_type[i] = tb_calc_aligned_size(1, attr_size, 16);
          vertex_size += attr_size_per_type[i];
        }
      }
    }

    geom_size = index_size + vertex_size;
  }

  uint64_t attr_offset_per_type[cgltf_attribute_type_max_enum] = {0};
  {
    uint64_t offset = index_size;
    for (uint32_t i = 0; i < cgltf_attribute_type_max_enum; ++i) {
      tb_auto attr_size = attr_size_per_type[i];
      if (attr_size > 0) {
        attr_offset_per_type[i] = offset;
        offset += attr_size;
      }
    }
  }

  // Create space for the mesh on the GPU
  void *ptr = NULL;
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = geom_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    tb_rnd_sys_create_gpu_buffer(rnd_sys, &create_info, "gltf_mesh",
                                 &data.gpu_buffer, &data.host_buffer, &ptr);
  }

  // Read the cgltf mesh into the driver owned memory
  {
    uint64_t idx_offset = 0;
    uint64_t vertex_count = 0;
    cgltf_size attr_count = 0;
    for (cgltf_size prim_idx = 0; prim_idx < gltf_mesh->primitives_count;
         ++prim_idx) {
      tb_auto prim = &gltf_mesh->primitives[prim_idx];

      {
        tb_auto indices = prim->indices;
        tb_auto view = indices->buffer_view;
        cgltf_size src_size = indices->count * indices->stride;
        cgltf_size padded_size =
            tb_calc_aligned_size(indices->count, indices->stride, 16);

        // Decode the buffer
        cgltf_result res = tb_decompress_buffer_view(tb_global_alloc, view);
        TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

        void *src = ((uint8_t *)view->data) + indices->offset;
        void *dst = ((uint8_t *)(ptr)) + idx_offset;
        SDL_memcpy(dst, src, src_size); // NOLINT
        idx_offset += padded_size;
      }

      // Determine the order of attributes
      cgltf_size attr_order[6] = {0};
      {
        const cgltf_attribute_type req_order[6] = {
            cgltf_attribute_type_position, cgltf_attribute_type_normal,
            cgltf_attribute_type_tangent,  cgltf_attribute_type_texcoord,
            cgltf_attribute_type_joints,   cgltf_attribute_type_weights,
        };
        cgltf_size attr_target_idx = 0;
        for (uint32_t i = 0; i < 6; ++i) {
          bool found = false;
          for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
               ++attr_idx) {
            cgltf_attribute *attr = &prim->attributes[attr_idx];
            if (attr->type == req_order[i]) {
              attr_order[attr_target_idx] = attr_idx;
              attr_target_idx++;
              if (prim_idx == 0) {
                attr_count++;
              }
              found = true;
            }
            if (found) {
              break;
            }
          }
        }
      }

      for (cgltf_size attr_idx = 0; attr_idx < attr_count; ++attr_idx) {
        cgltf_attribute *attr = &prim->attributes[attr_order[attr_idx]];
        cgltf_accessor *accessor = attr->data;
        cgltf_buffer_view *view = accessor->buffer_view;

        uint64_t mesh_vert_offset = vertex_count * attr->data->stride;
        uint64_t vtx_offset =
            attr_offset_per_type[attr->type] + mesh_vert_offset;

        size_t src_size = accessor->stride * accessor->count;

        // Decode the buffer
        cgltf_result res = tb_decompress_buffer_view(tb_global_alloc, view);
        TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

        void *src = ((uint8_t *)view->data) + accessor->offset;
        void *dst = ((uint8_t *)(ptr)) + vtx_offset;
        SDL_memcpy(dst, src, src_size); // NOLINT
      }

      vertex_count += prim->attributes[0].data->count;
    }

    // Construct one write per primitive
    {
      static const VkFormat
          attr_formats_per_type[cgltf_attribute_type_max_enum] = {
              VK_FORMAT_UNDEFINED,         VK_FORMAT_R16G16B16A16_SINT,
              VK_FORMAT_R8G8B8A8_SNORM,    VK_FORMAT_R8G8B8A8_SNORM,
              VK_FORMAT_R16G16_SINT,       VK_FORMAT_R8G8B8A8_UNORM,
              VK_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R8G8B8A8_SINT,
          };
      static const int32_t attr_idx_per_type[cgltf_attribute_type_max_enum] = {
          -1, 0, 1, 2, 3, -1, 5, 6, -1,
      };

      // Create one buffer view for indices
      {
        VkFormat idx_format = VK_FORMAT_R16_UINT;
        if (data.idx_type == VK_INDEX_TYPE_UINT32) {
          idx_format = VK_FORMAT_R32_UINT;
        }
        VkBufferViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .buffer = data.gpu_buffer.buffer,
            .offset = 0,
            .range = index_size,
            .format = idx_format,
        };
        tb_rnd_create_buffer_view(rnd_sys, &create_info, "Mesh Index View",
                                  &data.index_view);
      }

      for (size_t attr_idx = 0; attr_idx < attr_count; ++attr_idx) {
        cgltf_attribute *attr = &gltf_mesh->primitives[0].attributes[attr_idx];

        // Create a buffer view per attribute
        VkBufferViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .buffer = data.gpu_buffer.buffer,
            .offset = attr_offset_per_type[attr->type],
            .range = VK_WHOLE_SIZE,
            .format = attr_formats_per_type[attr->type],
        };
        tb_rnd_create_buffer_view(
            rnd_sys, &create_info, "Mesh Attribute View",
            &data.attr_views[attr_idx_per_type[attr->type]]);
      }
    }

    // Make sure to flush the gpu alloc if necessary
    tb_flush_alloc(rnd_sys, data.gpu_buffer.alloc);
  }

  TracyCZoneEnd(ctx);
  return data;
}

void tb_load_gltf_mesh_task(const void *args) {
  TracyCZoneN(ctx, "Load GLTF Mesh Task", true);
  tb_auto load_args = (const TbLoadGLTFMeshArgs *)args;
  tb_auto rnd_sys = load_args->common.rnd_sys;
  TbMesh2 mesh = load_args->common.mesh;

  tb_auto path = load_args->gltf.path;
  tb_auto index = load_args->gltf.index;

  tb_auto data = tb_read_glb(tb_thread_alloc, path);

  cgltf_mesh *gltf_mesh = &data->meshes[index];

  // Queue upload of mesh data to the GPU
  TbMeshData mesh_data = {0};
  if (mesh != 0) {
    mesh_data = tb_load_gltf_mesh(rnd_sys, gltf_mesh);
  }

  // Launch pinned task to handle loading signals on main thread
  TbMeshLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .mesh = mesh,
      .comp = mesh_data,
      .source_path = path,
      .gltf_mesh = gltf_mesh,
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
                .rnd_sys = ecs_singleton_get_mut(it->world, TbRenderSystem),
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

void tb_queue_gltf_submesh_loads(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Queue GLTF Submesh Loads", true);
  tb_auto counter = ecs_field(it, TbSubMeshQueueCounter, 1);
  tb_auto reqs = ecs_field(it, TbSubMeshGLTFLoadRequest, 2);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelMeshLoads) {
      break;
    }

    TbMesh2 mesh = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto gltf_mesh = req.gltf_mesh;
    tb_auto source_path = req.path;

    // As we go through submeshes we also want to construct an AABB for this
    // mesh
    TbAABB mesh_aabb = tb_aabb_init();

    // Meshes are uploaded so now we just need to setup submeshes
    uint32_t index_offset = 0;
    uint32_t vertex_offset = 0;
    for (cgltf_size i = 0; i < gltf_mesh->primitives_count; ++i) {
      tb_auto prim = &gltf_mesh->primitives[i];

      // Create an entity for this submesh which is a child of the mesh entity
      TbSubMesh2 submesh = ecs_new_entity(it->world, 0);
      ecs_add_pair(it->world, submesh, EcsChildOf, mesh);

      TbSubMesh2Data submesh_data = {
          .vertex_offset = vertex_offset,
          .vertex_count = prim->attributes[0].data->count,
      };
      vertex_offset += submesh_data.vertex_count;

      // If no material is provided we use a default
      const cgltf_material *material = prim->material;
      if (material == NULL) {
        submesh_data.material =
            tb_get_default_mat(it->world, TB_MAT_USAGE_SCENE);
      } else {
        submesh_data.material = tb_mat_sys_load_gltf_mat(
            it->world, source_path, material->name, TB_MAT_USAGE_SCENE);
      }
      // tb_free(tb_global_alloc, (void *)source_path);

      // Determine index size and count
      {
        const cgltf_accessor *indices = prim->indices;

        submesh_data.index_count = indices->count;
        submesh_data.index_offset = index_offset;

        // calculate the aligned size
        size_t index_size =
            tb_calc_aligned_size(indices->count, indices->stride, 16);
        // calculate number of indices that represent that aligned size
        index_offset += (index_size / indices->stride);
      }

      // Determine input permutation and attribute count
      {
        uint64_t vertex_attributes = 0;
        for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
             ++attr_idx) {
          cgltf_attribute_type type = prim->attributes[attr_idx].type;
          int32_t index = prim->attributes[attr_idx].index;
          if ((type == cgltf_attribute_type_position ||
               type == cgltf_attribute_type_normal ||
               type == cgltf_attribute_type_tangent ||
               type == cgltf_attribute_type_texcoord) &&
              index == 0) {
            if (type == cgltf_attribute_type_position) {
              vertex_attributes |= TB_INPUT_PERM_POSITION;
            } else if (type == cgltf_attribute_type_normal) {
              vertex_attributes |= TB_INPUT_PERM_NORMAL;
            } else if (type == cgltf_attribute_type_tangent) {
              vertex_attributes |= TB_INPUT_PERM_TANGENT;
            } else if (type == cgltf_attribute_type_texcoord) {
              vertex_attributes |= TB_INPUT_PERM_TEXCOORD0;
            }
          }
        }
        submesh_data.vertex_perm = vertex_attributes;
      }

      // Read AABB from gltf
      TbAABB submesh_aabb = tb_aabb_init();
      {
        const cgltf_attribute *pos_attr = NULL;
        // Find position attribute
        for (size_t i = 0; i < prim->attributes_count; ++i) {
          tb_auto attr = &prim->attributes[i];
          if (attr->type == cgltf_attribute_type_position) {
            pos_attr = attr;
            break;
          }
        }

        TB_CHECK(pos_attr, "Expected a position attribute");
        TB_CHECK(pos_attr->type == cgltf_attribute_type_position,
                 "Unexpected vertex attribute type");

        float *min = pos_attr->data->min;
        float *max = pos_attr->data->max;

        tb_aabb_add_point(&submesh_aabb, tb_f3(min[0], min[1], min[2]));
        tb_aabb_add_point(&submesh_aabb, tb_f3(max[0], max[1], max[2]));
      }
      ecs_set_ptr(it->world, submesh, TbAABB, &submesh_aabb);
      ecs_set_ptr(it->world, submesh, TbSubMesh2Data, &submesh_data);
      ecs_add(it->world, submesh, TbSubMeshParsed);

      tb_aabb_add_point(&mesh_aabb, submesh_aabb.min);
      tb_aabb_add_point(&mesh_aabb, submesh_aabb.max);
    }
    ecs_set_ptr(it->world, mesh, TbAABB, &mesh_aabb);

    SDL_AtomicIncRef(counter);
    ecs_remove(it->world, mesh, TbSubMeshGLTFLoadRequest);
  }

  TracyCZoneEnd(ctx);
}

void tb_reset_mesh_queue_count(ecs_iter_t *it) {
  tb_auto mesh_counter = ecs_field(it, TbMeshQueueCounter, 1);
  tb_auto submesh_counter = ecs_field(it, TbSubMeshQueueCounter, 2);

  SDL_AtomicSet(mesh_counter, 0);
  SDL_AtomicSet(submesh_counter, 0);
}

void tb_mesh_loading_phase(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Mesh Loading Phase", true);
  tb_auto mesh_ctx = ecs_field(it, TbMeshCtx, 1);

  if (mesh_ctx->owned_mesh_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  uint64_t loaded_mesh_count = 0;
  ecs_iter_t mesh_it = ecs_query_iter(it->world, mesh_ctx->loaded_mesh_query);
  while (ecs_query_next(&mesh_it)) {
    loaded_mesh_count += mesh_it.count;
  }

  // This phase is over when there are no more meshes to load
  if (mesh_ctx->owned_mesh_count == loaded_mesh_count) {
    ecs_remove(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseLoading);
    ecs_add(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseLoaded);
    mesh_ctx->pool_update_counter = 0;
  }

  TracyCZoneEnd(ctx);
}

void tb_update_mesh_pool(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Update Mesh Descriptors", true);

  tb_auto mesh_ctx = ecs_field(it, TbMeshCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);

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

  // Resize the pool if necessary
  if (mesh_count != tb_rnd_frame_desc_pool_get_desc_count(
                        rnd_sys, mesh_ctx->frame_set_pool.pools)) {

    const uint32_t view_count = TB_INPUT_PERM_COUNT + 1; // +1 for index buffer

    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = view_count,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = view_count * 4,
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
                                mesh_ctx->frame_set_pool.pools, view_count,
                                mesh_count);
  }

  // If we had to resize the pool, all meshes are dirty
  mesh_it = ecs_query_iter(it->world, mesh_ctx->loaded_mesh_query);
  while (ecs_query_next(&mesh_it)) {
    tb_auto ecs = mesh_it.world;
    for (int32_t i = 0; i < mesh_it.count; ++i) {
      tb_auto mesh_ent = mesh_it.entities[i];
      ecs_add(ecs, mesh_ent, TbNeedsDescriptorUpdate);
      ecs_remove(ecs, mesh_ent, TbDescriptorReady);
      if (!ecs_has(ecs, mesh_ent, TbDescriptorCounter)) {
        ecs_set(ecs, mesh_ent, TbDescriptorCounter, {0});
      } else {
        tb_auto counter = ecs_get_mut(ecs, mesh_ent, TbDescriptorCounter);
        SDL_AtomicSet(counter, 0);
      }
    }
  }

  mesh_ctx->pool_update_counter++;

  // One pool must be resized per frame
  if (mesh_ctx->pool_update_counter >= TB_MAX_FRAME_STATES) {
    ecs_remove(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseLoaded);
    ecs_add(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseWriting);

    // Reset counter for next phase
    mesh_ctx->pool_update_counter = 0;
  }

  TracyCZoneEnd(ctx);
}

void tb_write_mesh_descriptors(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Write Mesh Descriptors", true);

  tb_auto mesh_ctx = ecs_field(it, TbMeshCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto world = ecs_singleton_get(it->world, TbWorldRef)->world;

  // Accumulate the number of meshes
  uint64_t mesh_count = 0;

  ecs_iter_t mesh_it = ecs_query_iter(it->world, mesh_ctx->dirty_mesh_query);
  while (ecs_query_next(&mesh_it)) {
    mesh_count += mesh_it.count;
  }

  if (mesh_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  const uint32_t view_count = TB_INPUT_PERM_COUNT + 1; // +1 for index buffer

  // Write all dirty meshes into the descriptor set table
  uint32_t mesh_idx = 0;
  mesh_it = ecs_query_iter(it->world, mesh_ctx->dirty_mesh_query);

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
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
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
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            .dstSet = tb_rnd_frame_desc_pool_get_set(
                rnd_sys, mesh_ctx->frame_set_pool.pools, attr_idx + 1),
            .dstBinding = 0,
            .pTexelBufferView = &TB_DYN_ARR_AT(buf_views, mesh_idx),
        };
        TB_DYN_ARR_APPEND(writes, write);
      }

      // Mesh is now ready to be referenced elsewhere
      ecs_set(it->world, mesh_it.entities[i], TbMeshIndex, {mesh_idx});
      ecs_add(it->world, mesh_it.entities[i], TbUpdatingDescriptor);
      mesh_idx++;
    }
  }
  tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);

  mesh_ctx->pool_update_counter++;

  // One write must be enqueued per frame
  if (mesh_ctx->pool_update_counter >= TB_MAX_FRAME_STATES) {
    ecs_remove(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseWriting);
    ecs_add(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseWritten);
  }

  TracyCZoneEnd(ctx);
}

void tb_mesh_phase_written(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Mesh Written Phase", true);
  tb_auto mesh_ctx = ecs_field(it, TbMeshCtx, 1);

  // Accumulate the number of meshes
  uint64_t parsed_meshes = 0;

  ecs_iter_t mesh_it = ecs_query_iter(it->world, mesh_ctx->dirty_mesh_query);
  while (ecs_query_next(&mesh_it)) {
    parsed_meshes += mesh_it.count;
  }

  // When we have no more parsed meshes that means all meshes are ready
  // This is cheaper than counting up
  if (parsed_meshes == 0) {
    ecs_remove(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseWritten);
    ecs_add(it->world, ecs_id(TbMeshCtx), TbMeshLoadPhaseReady);
  }

  TracyCZoneEnd(ctx);
}

void tb_check_submesh_readiness(ecs_iter_t *it) {
  tb_auto submesh_data = ecs_field(it, TbSubMesh2Data, 1);
  for (int32_t i = 0; i < it->count; ++i) {
    TbSubMesh2 submesh = it->entities[i];
    tb_auto data = &submesh_data[i];
    // Submeshes are ready when dependant materials are ready
    if (tb_is_material_ready(it->world, data->material)) {
      ecs_remove(it->world, submesh, TbSubMeshParsed);
      ecs_add(it->world, submesh, TbSubMeshReady);
    }
  }
}

void tb_check_mesh_readiness(ecs_iter_t *it) {
  for (int32_t i = 0; i < it->count; ++i) {
    TbMesh2 mesh = it->entities[i];

    // Check that all children are ready
    tb_auto child_iter = ecs_children(it->world, mesh);
    bool children_ready = false;
    while (ecs_children_next(&child_iter)) {
      if (children_ready == false) {
        children_ready = child_iter.count > 0;
      }
      for (int32_t child_i = 0; child_i < child_iter.count; ++child_i) {
        ecs_entity_t child_ent = child_iter.entities[child_i];
        // If the child is a submesh
        if (ecs_has(it->world, child_ent, TbSubMesh2Data)) {
          // Ensure that it is ready
          if (!ecs_has(it->world, child_ent, TbSubMeshReady)) {
            children_ready = false;
            break;
          }
        }
      }
    }

    // If all submeshes are ready, we are ready
    if (children_ready) {
      ecs_remove(it->world, mesh, TbMeshParsed);
      ecs_add(it->world, mesh, TbMeshReady);
    }
  }
}

// Toybox Glue

void tb_register_mesh2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMeshCtx);
  ECS_COMPONENT_DEFINE(ecs, TbMeshData);
  ECS_COMPONENT_DEFINE(ecs, TbSubMesh2Data);
  ECS_COMPONENT_DEFINE(ecs, TbSubMeshQueueCounter);
  ECS_COMPONENT_DEFINE(ecs, TbMeshQueueCounter);
  ECS_COMPONENT_DEFINE(ecs, TbMeshIndex);
  ECS_COMPONENT_DEFINE(ecs, TbAABB);
  ECS_COMPONENT_DEFINE(ecs, TbMeshGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbSubMeshGLTFLoadRequest);
  ECS_TAG_DEFINE(ecs, TbMeshLoaded);
  ECS_TAG_DEFINE(ecs, TbMeshParsed);
  ECS_TAG_DEFINE(ecs, TbMeshReady);
  ECS_TAG_DEFINE(ecs, TbSubMeshParsed);
  ECS_TAG_DEFINE(ecs, TbSubMeshReady);
  ECS_TAG_DEFINE(ecs, TbMeshLoadPhaseLoading);
  ECS_TAG_DEFINE(ecs, TbMeshLoadPhaseLoaded);
  ECS_TAG_DEFINE(ecs, TbMeshLoadPhaseWriting);
  ECS_TAG_DEFINE(ecs, TbMeshLoadPhaseWritten);
  ECS_TAG_DEFINE(ecs, TbMeshLoadPhaseReady);

  ECS_SYSTEM(ecs, tb_queue_gltf_mesh_loads,
             EcsPreUpdate, [inout] TbTaskScheduler(TbTaskScheduler),
             [inout] TbMeshQueueCounter(TbMeshQueueCounter),
             [in] TbMeshGLTFLoadRequest);
  ECS_SYSTEM(ecs, tb_queue_gltf_submesh_loads,
             EcsPreUpdate, [inout] TbSubMeshQueueCounter(TbSubMeshQueueCounter),
             [in] TbSubMeshGLTFLoadRequest);

  ECS_SYSTEM(ecs, tb_reset_mesh_queue_count,
             EcsPostUpdate, [inout] TbMeshQueueCounter(TbMeshQueueCounter),
             [inout] TbSubMeshQueueCounter(TbSubMeshQueueCounter));

  // While meshes are moving from parsed to ready states
  ECS_SYSTEM(ecs, tb_mesh_loading_phase, EcsPreFrame, [in] TbMeshCtx(TbMeshCtx),
             TbMeshLoadPhaseLoading);
  // When all meshes are loaded we start making them available to shaders
  ECS_SYSTEM(ecs, tb_update_mesh_pool, EcsPostUpdate, [in] TbMeshCtx(TbMeshCtx),
             [in] TbRenderSystem(TbRenderSystem), TbMeshLoadPhaseLoaded);
  // System that ticks as we ensure mesh descriptors are written
  ECS_SYSTEM(ecs, tb_write_mesh_descriptors, EcsPreStore,
             [in] TbMeshCtx(TbMeshCtx), [in] TbRenderSystem(TbRenderSystem),
             TbMeshLoadPhaseWriting);
  // When all meshes are ready the phase we will be done
  ECS_SYSTEM(ecs, tb_mesh_phase_written, EcsOnStore, [in] TbMeshCtx(TbMeshCtx),
             TbMeshLoadPhaseWritten);

  ECS_SYSTEM(ecs, tb_check_submesh_readiness,
             EcsPreStore, [in] TbSubMesh2Data, [in] TbSubMeshParsed);
  ECS_SYSTEM(ecs, tb_check_mesh_readiness, EcsPreStore, [in] TbMeshParsed);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  TbMeshCtx ctx = {
      .loaded_mesh_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(TbMeshData), .inout = EcsIn},
                                  {.id = ecs_id(TbMeshLoaded)},
                              }}),
      .dirty_mesh_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(TbMeshData), .inout = EcsIn},
                                  {.id = ecs_id(TbMeshLoaded)},
                                  {.id = ecs_id(TbNeedsDescriptorUpdate)},
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

  {
    TbMeshQueueCounter queue_count = {0};
    SDL_AtomicSet(&queue_count, 0);
    ecs_singleton_set_ptr(ecs, TbMeshQueueCounter, &queue_count);
  }
  {
    TbSubMeshQueueCounter queue_count = {0};
    SDL_AtomicSet(&queue_count, 0);
    ecs_singleton_set_ptr(ecs, TbSubMeshQueueCounter, &queue_count);
  }
}

void tb_unregister_mesh2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);

  ecs_query_fini(ctx->loaded_mesh_query);
  ecs_query_fini(ctx->dirty_mesh_query);

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

void tb_mesh_sys_reserve_mesh_count(ecs_world_t *ecs, uint32_t mesh_count) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  ctx->owned_mesh_count = mesh_count;
  ecs_remove(ecs, ecs_id(TbMeshCtx), TbMeshLoadPhaseLoaded);
  ecs_remove(ecs, ecs_id(TbMeshCtx), TbMeshLoadPhaseWriting);
  ecs_remove(ecs, ecs_id(TbMeshCtx), TbMeshLoadPhaseWritten);
  ecs_remove(ecs, ecs_id(TbMeshCtx), TbMeshLoadPhaseReady);
  ecs_add(ecs, ecs_id(TbMeshCtx), TbMeshLoadPhaseLoading);
}

TbMesh2 tb_mesh_sys_load_gltf_mesh(ecs_world_t *ecs, const char *path,
                                   uint32_t index) {
  const uint32_t max_mesh_name_len = 256;
  char mesh_name[max_mesh_name_len] = {0};
  SDL_snprintf(mesh_name, max_mesh_name_len, "mesh_%d", index);

  // If an entity already exists with this name it is either loading or loaded
  TbMesh2 mesh_ent = ecs_lookup_child(ecs, ecs_id(TbMeshCtx), mesh_name);
  if (mesh_ent != 0) {
    return mesh_ent;
  }

  // Create a mesh entity
  mesh_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, mesh_ent, mesh_name);

  // It is a child of the mesh system context singleton
  ecs_add_pair(ecs, mesh_ent, EcsChildOf, ecs_id(TbMeshCtx));

  // Need to copy strings for task safety
  // Tasks are responsible for freeing these names
  const size_t path_len = SDL_strnlen(path, 256) + 1;
  char *path_cpy = tb_alloc_nm_tp(tb_global_alloc, path_len, char);
  SDL_strlcpy(path_cpy, path, path_len);

  // Append a mesh load request onto the entity to schedule loading
  ecs_set(ecs, mesh_ent, TbMeshGLTFLoadRequest, {path_cpy, index});
  ecs_add(ecs, mesh_ent, TbNeedsDescriptorUpdate);

  return mesh_ent;
}

bool tb_is_mesh_ready(ecs_world_t *ecs, TbMesh2 mesh_ent) {
  return ecs_has(ecs, mesh_ent, TbMeshReady) &&
         ecs_has(ecs, mesh_ent, TbDescriptorReady);
}
