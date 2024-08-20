#include "tb_mesh_system.h"

#include "tb_assets.h"
#include "tb_dyn_desc_pool.h"
#include "tb_gltf.h"
#include "tb_log.h"
#include "tb_material_system.h"
#include "tb_task_scheduler.h"
#include "tb_util.h"

// Internals

#define TB_MAX_READY_CHECKS_PER_FRAME 16

// Mesh system probably shouldn't own this
ECS_COMPONENT_DECLARE(TbAABB);

static const int32_t TbMaxParallelMeshLoads = 16;

typedef SDL_AtomicInt TbMeshQueueCounter;
ECS_COMPONENT_DECLARE(TbMeshQueueCounter);
typedef SDL_AtomicInt TbSubMeshQueueCounter;
ECS_COMPONENT_DECLARE(TbSubMeshQueueCounter);

ECS_COMPONENT_DECLARE(TbSubMesh2Data);

typedef struct TbMeshCtx {
  uint32_t owned_mesh_count;
  VkDescriptorSetLayout set_layout;
  TbDynDescPool idx_desc_pool;
  TbDynDescPool pos_desc_pool;
  TbDynDescPool norm_desc_pool;
  TbDynDescPool tan_desc_pool;
  TbDynDescPool uv0_desc_pool;

  TbDescriptorBuffer idx_desc_buf;
  TbDescriptorBuffer pos_desc_buf;
  TbDescriptorBuffer norm_desc_buf;
  TbDescriptorBuffer tan_desc_buf;
  TbDescriptorBuffer uv0_desc_buf;
} TbMeshCtx;
ECS_COMPONENT_DECLARE(TbMeshCtx);

typedef struct TbMeshData {
  VkIndexType idx_type;
  TbHostBuffer host_buffer;
  TbBuffer gpu_buffer;
#if TB_USE_DESC_BUFFER == 1
  VkDescriptorAddressInfoEXT index_addr;
  VkDescriptorAddressInfoEXT attribute_addr[TB_INPUT_PERM_COUNT];
#else
  VkBufferView index_view;
  VkBufferView attr_views[TB_INPUT_PERM_COUNT];
#endif
} TbMeshData;
ECS_COMPONENT_DECLARE(TbMeshData);

ECS_COMPONENT_DECLARE(TbMeshIndex);

// Describes the creation of a mesh that lives in a GLB file
typedef struct TbMeshGLTFLoadRequest {
  cgltf_data *data;
  uint32_t index;
} TbMeshGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbMeshGLTFLoadRequest);

typedef struct TbSubMeshGLTFLoadRequest {
  const cgltf_data *data;
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
  const cgltf_data *data;
  const cgltf_mesh *gltf_mesh;
  TbMeshQueueCounter *counter;
} TbMeshLoadedArgs;

void tb_mesh_loaded(const void *args) {
  TB_TRACY_SCOPE("Mesh Loaded");
  tb_auto loaded_args = (const TbMeshLoadedArgs *)args;
  tb_auto ecs = loaded_args->ecs;
  tb_auto mesh = loaded_args->mesh;
  tb_auto data = loaded_args->data;
  tb_auto gltf_mesh = loaded_args->gltf_mesh;
  tb_auto counter = loaded_args->counter;
  if (mesh == 0) {
    TB_CHECK(false, "Mesh load failed. Do we need to retry?");
  }

  TbSubMeshGLTFLoadRequest submesh_req = {
      .data = data,
      .gltf_mesh = gltf_mesh,
  };

  ecs_add(ecs, mesh, TbMeshLoaded);
  ecs_add(ecs, mesh, TbMeshParsed);
  ecs_set_ptr(ecs, mesh, TbMeshData, &loaded_args->comp);
  ecs_set_ptr(ecs, mesh, TbSubMeshGLTFLoadRequest, &submesh_req);

  SDL_AtomicDecRef(counter);
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
  TbMeshQueueCounter *counter;
} TbLoadGLTFMeshArgs;

TbMeshData tb_load_gltf_mesh(TbRenderSystem *rnd_sys,
                             const cgltf_mesh *gltf_mesh) {
  TB_TRACY_SCOPE("Load GLTF Mesh");
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
  TbBufferCopy buf_copy = {0};
  {

    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = geom_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    char mesh_name[512] = {0};

    tb_rnd_sys_create_gpu_buffer_noup(rnd_sys, &create_info, mesh_name,
                                      &data.gpu_buffer, &data.host_buffer,
                                      &ptr);

    buf_copy = (TbBufferCopy){
        .dst = data.gpu_buffer.buffer,
        .src = data.host_buffer.buffer,
        .region =
            {
                .size = create_info.size,
            },
    };
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
        cgltf_result res = tb_decompress_buffer_view(tb_thread_alloc, view);
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
        cgltf_result res = tb_decompress_buffer_view(tb_thread_alloc, view);
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
        TB_CHECK(index_size, "Unexpected index size of 0");

#if TB_USE_DESC_BUFFER == 1
        data.index_addr = (VkDescriptorAddressInfoEXT){
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
            .address = data.gpu_buffer.address,
            .range = index_size,
            .format = idx_format,
        };
#else
        VkBufferViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .buffer = data.gpu_buffer.buffer,
            .offset = 0,
            .range = index_size,
            .format = idx_format,
        };
        tb_rnd_create_buffer_view(rnd_sys, &create_info, "Mesh Index View",
                                  &data.index_view);
#endif
      }

#if TB_USE_DESC_BUFFER == 1
      // Set a default buffer for each primitive
      for (size_t attr_idx = 0; attr_idx < TB_INPUT_PERM_COUNT; ++attr_idx) {
        data.attribute_addr[attr_idx] = (VkDescriptorAddressInfoEXT){
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
            .range = VK_WHOLE_SIZE,
        };
      }
#endif

      for (size_t attr_idx = 0; attr_idx < attr_count; ++attr_idx) {
        cgltf_attribute *attr = &gltf_mesh->primitives[0].attributes[attr_idx];

#if TB_USE_DESC_BUFFER == 1
        VkDeviceAddress address =
            data.gpu_buffer.address + attr_offset_per_type[attr->type];
        data.attribute_addr[attr_idx_per_type[attr->type]] =
            (VkDescriptorAddressInfoEXT){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                .address = address,
                .range = attr_size_per_type[attr->type],
                .format = attr_formats_per_type[attr->type],
            };
#else
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
#endif
      }
    }

    // Make sure to flush the gpu alloc if necessary
    tb_flush_alloc(rnd_sys, data.gpu_buffer.alloc);

    // Only enqueue this buffer upload request after the allocation is flushed
    if (buf_copy.src != NULL) {
      tb_rnd_upload_buffers(rnd_sys, &buf_copy, 1);
    }
  }
  return data;
}

void tb_load_gltf_mesh_task(const void *args) {
  TB_TRACY_SCOPE("Load GLTF Mesh Task");
  tb_auto load_args = (const TbLoadGLTFMeshArgs *)args;
  tb_auto rnd_sys = load_args->common.rnd_sys;
  tb_auto mesh = load_args->common.mesh;
  tb_auto data = load_args->gltf.data;
  tb_auto index = load_args->gltf.index;
  tb_auto counter = load_args->counter;

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
      .data = data,
      .gltf_mesh = gltf_mesh,
      .counter = counter,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbMeshLoadedArgs));
}

// Systems

void tb_queue_gltf_mesh_loads(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Queue GLTF Mesh Loads");

  tb_auto ecs = it->world;

  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto counter = ecs_field(it, TbMeshQueueCounter, 2);
  tb_auto reqs = ecs_field(it, TbMeshGLTFLoadRequest, 3);

  tb_auto mesh_ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);

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
                .ecs = ecs,
                .rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem),
                .mesh = ent,
                .enki = enki,
                .loaded_task = loaded_task,
            },
        .gltf = req,
        .counter = counter,
    };
    TbTask load_task = tb_async_task(enki, tb_load_gltf_mesh_task, &args,
                                     sizeof(TbLoadGLTFMeshArgs));
    // Apply task component to mesh entity
    ecs_set(ecs, ent, TbTask, {load_task});
    SDL_AtomicIncRef(counter);
    mesh_ctx->owned_mesh_count++;

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(ecs, ent, TbMeshGLTFLoadRequest);
  }
}

typedef struct TbSubMeshLoadArgs {
  ecs_world_t *ecs;
  TbSubMeshQueueCounter *counter;
  TbMesh2 mesh;
  TbSubMeshGLTFLoadRequest req;
} TbSubMeshLoadArgs;

void tb_load_submeshes_task(const void *args) {
  TB_TRACY_SCOPE("Load Submeshes Task");
  tb_auto load_args = (const TbSubMeshLoadArgs *)args;
  tb_auto ecs = load_args->ecs;
  tb_auto counter = load_args->counter;
  tb_auto mesh = load_args->mesh;
  tb_auto gltf_mesh = load_args->req.gltf_mesh;
  tb_auto data = load_args->req.data;

  // As we go through submeshes we also want to construct an AABB for this
  // mesh
  TbAABB mesh_aabb = tb_aabb_init();

  // Meshes are uploaded so now we just need to setup submeshes
  uint32_t index_offset = 0;
  uint32_t vertex_offset = 0;
  for (cgltf_size i = 0; i < gltf_mesh->primitives_count; ++i) {
    tb_auto prim = &gltf_mesh->primitives[i];

    // Create an entity for this submesh which is a child of the mesh entity
    TbSubMesh2 submesh = ecs_new_entity(ecs, 0);
    ecs_add_pair(ecs, submesh, EcsChildOf, mesh);

    TbSubMesh2Data submesh_data = {
        .vertex_offset = vertex_offset,
        .vertex_count = prim->attributes[0].data->count,
    };
    vertex_offset += submesh_data.vertex_count;

    // If no material is provided we use a default
    const cgltf_material *material = prim->material;
    if (material == NULL) {
      submesh_data.material = tb_get_default_mat(ecs, TB_MAT_USAGE_SCENE);
    } else {
      submesh_data.material = tb_mat_sys_load_gltf_mat(
          ecs, data, material->name, TB_MAT_USAGE_SCENE);
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
    ecs_set_ptr(ecs, submesh, TbAABB, &submesh_aabb);
    ecs_set_ptr(ecs, submesh, TbSubMesh2Data, &submesh_data);
    ecs_add(ecs, submesh, TbSubMeshParsed);

    tb_aabb_add_point(&mesh_aabb, submesh_aabb.min);
    tb_aabb_add_point(&mesh_aabb, submesh_aabb.max);
  }
  ecs_set_ptr(ecs, mesh, TbAABB, &mesh_aabb);
  ecs_remove(ecs, mesh, TbSubMeshGLTFLoadRequest);

  // Mesh submeshes are loaded
  SDL_AtomicDecRef(counter);
}

void tb_queue_gltf_submesh_loads(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Queue GLTF Submesh Loads");
  tb_auto counter = ecs_field(it, TbSubMeshQueueCounter, 1);
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 2);
  tb_auto reqs = ecs_field(it, TbSubMeshGLTFLoadRequest, 3);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelMeshLoads) {
      break;
    }

    // Enqueue task so that the submesh loads outside of a system where the
    // ecs will be in a writable state
    TbSubMeshLoadArgs args = {
        .ecs = it->world,
        .mesh = it->entities[i],
        .req = reqs[i],
        .counter = counter,
    };
    tb_auto task = tb_create_pinned_task(enki, tb_load_submeshes_task, &args,
                                         sizeof(TbSubMeshLoadArgs));
    tb_launch_pinned_task(enki, task);

    SDL_AtomicIncRef(counter);
  }
}

void tb_write_mesh_attr_desc(ecs_world_t *ecs, TbMeshCtx *ctx,
                             uint32_t mesh_count, const ecs_entity_t *entities,
                             const TbMeshData *data, TbAllocator gp_alloc,
                             TbAllocator tmp_alloc, uint32_t attr_idx) {
  TB_DYN_ARR_OF(TbDynDescWrite) writes = {0};
  TB_DYN_ARR_RESET(writes, gp_alloc, mesh_count);

  for (uint32_t i = 0; i < mesh_count; ++i) {
    tb_auto mesh = &data[i];
    VkBufferView attr_view = mesh->index_view;
    if (attr_idx > 0) {
      attr_view = mesh->attr_views[attr_idx - 1];
    }
    TbDynDescWrite write = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        .desc.texel_buffer = attr_view,
    };
    TB_DYN_ARR_APPEND(writes, write);
  }

  const uint32_t write_count = TB_DYN_ARR_SIZE(writes);
  if (write_count > 0) {
    tb_auto indices = tb_alloc_nm_tp(tmp_alloc, write_count, uint32_t);
    TbDynDescPool *pool = NULL;
    switch (attr_idx) {
    case 0:
      pool = &ctx->idx_desc_pool;
      break;
    case 1:
      pool = &ctx->pos_desc_pool;
      break;
    case 2:
      pool = &ctx->norm_desc_pool;
      break;
    case 3:
      pool = &ctx->tan_desc_pool;
      break;
    case 4:
      pool = &ctx->uv0_desc_pool;
      break;
    default:
      pool = NULL;
    }
    TB_CHECK(pool, "Failed to find pool for attribute");
    tb_write_dyn_desc_pool(pool, write_count, writes.data, indices);

    // A bit hack but we always expect attr idx 0 (indices)
    // so this is when we can use the reported indices to mark
    // mesh components
    // Ideally we'd make sure the various attr pools are in sync but for now we
    // just pray
    if (attr_idx == 0) {
      TB_DYN_ARR_FOREACH(writes, i) {
        ecs_set(ecs, entities[i], TbMeshIndex, {indices[i]});
        tb_rnd_mark_descriptor(ecs, entities[i]);
      }
    }
  }
  TB_DYN_ARR_DESTROY(writes);
}

void tb_finalize_meshes(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Finalize Meshes");

  tb_auto ctx = ecs_field(it, TbMeshCtx, 1);
  tb_auto meshes = ecs_field(it, TbMeshData, 2);
  tb_auto world = ecs_singleton_get(it->world, TbWorldRef)->world;

  if (ctx->owned_mesh_count == 0 || it->count == 0) {
    return;
  }

  // HACK: known fixed number of mesh attributes
  for (uint32_t i = 0; i < 5; ++i) {
    tb_write_mesh_attr_desc(it->world, ctx, it->count, it->entities, meshes,
                            world->gp_alloc, world->tmp_alloc, i);
  }
}

void tb_update_mesh_pool(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Update Mesh Pool");

  tb_auto ctx = ecs_field(it, TbMeshCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  if (ctx->owned_mesh_count == 0) {
    return;
  }
  tb_tick_dyn_desc_pool(rnd_sys, &ctx->idx_desc_pool, "mesh_indices");
  tb_tick_dyn_desc_pool(rnd_sys, &ctx->pos_desc_pool, "mesh_positions");
  tb_tick_dyn_desc_pool(rnd_sys, &ctx->norm_desc_pool, "mesh_normals");
  tb_tick_dyn_desc_pool(rnd_sys, &ctx->tan_desc_pool, "mesh_tangents");
  tb_tick_dyn_desc_pool(rnd_sys, &ctx->uv0_desc_pool, "mesh_uv0s");
}

void tb_check_submesh_readiness(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Check Submesh Readiness");
  uint32_t count = 0;
  tb_auto submesh_data = ecs_field(it, TbSubMesh2Data, 1);
  for (int32_t i = 0; i < it->count; ++i) {
    TbSubMesh2 submesh = it->entities[i];
    tb_auto data = &submesh_data[i];
    // Submeshes are ready when dependant materials are ready
    if (tb_is_material_ready(it->world, data->material)) {
      ecs_remove(it->world, submesh, TbSubMeshParsed);
      ecs_add(it->world, submesh, TbSubMeshReady);
    }
    count++;
    if (count == TB_MAX_READY_CHECKS_PER_FRAME) {
      return;
    }
  }
}

void tb_check_mesh_readiness(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Check Mesh Readiness");
  uint32_t count = 0;
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

    count++;
    if (count == TB_MAX_READY_CHECKS_PER_FRAME) {
      return;
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

  ECS_SYSTEM(ecs, tb_queue_gltf_mesh_loads,
             EcsPreUpdate, [inout] TbTaskScheduler(TbTaskScheduler),
             [inout] TbMeshQueueCounter(TbMeshQueueCounter),
             [in] TbMeshGLTFLoadRequest);
  ECS_SYSTEM(ecs, tb_queue_gltf_submesh_loads, EcsPreUpdate,
             TbSubMeshQueueCounter(TbSubMeshQueueCounter),
             TbTaskScheduler(TbTaskScheduler), [in] TbSubMeshGLTFLoadRequest);

  // System that ticks as we ensure mesh descriptors are written
  ECS_SYSTEM(ecs, tb_finalize_meshes, EcsPostUpdate, [in] TbMeshCtx(TbMeshCtx),
             [in] TbMeshData, [in] TbMeshLoaded, [in] TbNeedsDescriptorUpdate,
             !TbUpdatingDescriptor, !TbDescriptorReady);
  // When all meshes are loaded we start making them available to shaders
  ECS_SYSTEM(ecs, tb_update_mesh_pool, EcsPreStore, [in] TbMeshCtx(TbMeshCtx),
             [in] TbRenderSystem(TbRenderSystem));

  ECS_SYSTEM(ecs, tb_check_submesh_readiness,
             EcsPreStore, [in] TbSubMesh2Data, [in] TbSubMeshParsed);
  ECS_SYSTEM(
      ecs, tb_check_mesh_readiness,
      EcsPreStore, [in] TbMeshData, [in] TbMeshParsed, [in] TbDescriptorReady);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  TbMeshCtx ctx = {0};

  // Create mesh descriptor set layout
  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
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
                    .descriptorCount = 4096, // HACK: High upper bound
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                },
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Mesh Attr Layout",
                             &ctx.set_layout);
  }

#if TB_USE_DESC_BUFFER == 1
  tb_create_descriptor_buffer(rnd_sys, ctx.set_layout, "Mesh Idx Descriptors",
                              4, &ctx.idx_desc_buf);
  tb_create_descriptor_buffer(rnd_sys, ctx.set_layout, "Mesh Pos Descriptors",
                              4, &ctx.pos_desc_buf);
  tb_create_descriptor_buffer(rnd_sys, ctx.set_layout, "Mesh Norm Descriptors",
                              4, &ctx.norm_desc_buf);
  tb_create_descriptor_buffer(rnd_sys, ctx.set_layout, "Mesh Tan Descriptors",
                              4, &ctx.tan_desc_buf);
  tb_create_descriptor_buffer(rnd_sys, ctx.set_layout, "Mesh UV0 Descriptors",
                              4, &ctx.uv0_desc_buf);
#else
  tb_create_dyn_desc_pool(rnd_sys, ctx.set_layout,
                          VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                          &ctx.idx_desc_pool, 0);
  tb_create_dyn_desc_pool(rnd_sys, ctx.set_layout,
                          VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                          &ctx.pos_desc_pool, 0);
  tb_create_dyn_desc_pool(rnd_sys, ctx.set_layout,
                          VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                          &ctx.norm_desc_pool, 0);
  tb_create_dyn_desc_pool(rnd_sys, ctx.set_layout,
                          VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                          &ctx.tan_desc_pool, 0);
  tb_create_dyn_desc_pool(rnd_sys, ctx.set_layout,
                          VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                          &ctx.uv0_desc_pool, 0);
#endif

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

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);

  tb_destroy_descriptor_buffer(rnd_sys, &ctx->idx_desc_buf);
  tb_destroy_descriptor_buffer(rnd_sys, &ctx->pos_desc_buf);
  tb_destroy_descriptor_buffer(rnd_sys, &ctx->norm_desc_buf);
  tb_destroy_descriptor_buffer(rnd_sys, &ctx->tan_desc_buf);
  tb_destroy_descriptor_buffer(rnd_sys, &ctx->uv0_desc_buf);

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

VkDescriptorSet tb_mesh_sys_get_idx_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->idx_desc_pool);
}
VkDescriptorSet tb_mesh_sys_get_pos_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->pos_desc_pool);
}
VkDescriptorSet tb_mesh_sys_get_norm_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->norm_desc_pool);
}
VkDescriptorSet tb_mesh_sys_get_tan_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->tan_desc_pool);
}
VkDescriptorSet tb_mesh_sys_get_uv0_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->uv0_desc_pool);
}

VkDescriptorBufferBindingInfoEXT tb_mesh_sys_get_idx_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  return tb_desc_buff_get_binding(&ctx->idx_desc_buf);
}

VkDescriptorBufferBindingInfoEXT tb_mesh_sys_get_pos_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  return tb_desc_buff_get_binding(&ctx->pos_desc_buf);
}

VkDescriptorBufferBindingInfoEXT tb_mesh_sys_get_norm_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  return tb_desc_buff_get_binding(&ctx->norm_desc_buf);
}

VkDescriptorBufferBindingInfoEXT tb_mesh_sys_get_tan_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  return tb_desc_buff_get_binding(&ctx->tan_desc_buf);
}

VkDescriptorBufferBindingInfoEXT tb_mesh_sys_get_uv0_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMeshCtx);
  return tb_desc_buff_get_binding(&ctx->uv0_desc_buf);
}

TbMesh2 tb_mesh_sys_load_gltf_mesh(ecs_world_t *ecs, cgltf_data *data,
                                   const char *path, const char *name,
                                   uint32_t index) {
  bool deferred = false;
  if (ecs_is_deferred(ecs)) {
    deferred = ecs_defer_end(ecs);
  }

  const uint32_t max_name_len = 512;
  char mesh_name[max_name_len] = {0};
  SDL_snprintf(mesh_name, max_name_len, "%s_%s", path, name);

  // If an entity already exists with this name it is either loading or loaded
  TbMesh2 mesh_ent = ecs_lookup_child(ecs, ecs_id(TbMeshCtx), mesh_name);
  if (mesh_ent != 0) {
    if (deferred) {
      ecs_defer_begin(ecs);
    }
    return mesh_ent;
  }

  TB_CHECK_RETURN(data, "Expected data", 0);

  // Create a mesh entity
  mesh_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, mesh_ent, mesh_name);

  // It is a child of the mesh system context singleton
  ecs_add_pair(ecs, mesh_ent, EcsChildOf, ecs_id(TbMeshCtx));

  // Append a mesh load request onto the entity to schedule loading
  ecs_set(ecs, mesh_ent, TbMeshGLTFLoadRequest, {data, index});
  tb_rnd_reset_descriptor_count(ecs, mesh_ent);

  if (deferred) {
    ecs_defer_begin(ecs);
  }

  return mesh_ent;
}

VkBuffer tb_mesh_sys_get_gpu_mesh(ecs_world_t *ecs, TbMesh2 mesh) {
  TB_CHECK(ecs_has(ecs, mesh, TbMeshData), "Entity must have mesh data");
  return ecs_get(ecs, mesh, TbMeshData)->gpu_buffer.buffer;
}

bool tb_is_mesh_ready(ecs_world_t *ecs, TbMesh2 mesh_ent) {
  return ecs_has(ecs, mesh_ent, TbMeshReady) &&
         ecs_has(ecs, mesh_ent, TbDescriptorReady);
}
