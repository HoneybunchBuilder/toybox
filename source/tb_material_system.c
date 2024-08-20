#include "tb_material_system.h"

#include "tb_assets.h"
#include "tb_common.h"
#include "tb_dyn_desc_pool.h"
#include "tb_gltf.h"
#include "tb_queue.h"
#include "tb_scene_material.h"
#include "tb_task_scheduler.h"
#include "tb_world.h"

static const int32_t TbMaxParallelMaterialLoads = 8;
static SDL_AtomicInt tb_parallel_mat_load_count = {0};

// Components

ECS_COMPONENT_DECLARE(TbMaterialUsage);

ECS_COMPONENT_DECLARE(TbMaterialData);

typedef struct TbMaterialDomainHandler {
  TbMaterialUsage usage;
  TbMaterialDomain domain;
  size_t type_size;
  TbMaterial2 default_mat;
} TbMaterialDomainHandler;
ECS_COMPONENT_DECLARE(TbMaterialDomainHandler);

typedef struct TbMaterialCtx {
  VkSampler sampler;        // Immutable sampler for material descriptor sets
  VkSampler shadow_sampler; // Immutable sampler for sampling shadow maps
  VkDescriptorSetLayout set_layout;
  uint32_t owned_mat_count;
  TbDynDescPool desc_pool;

  VkDescriptorSetLayout set_layout2;
  TbDescriptorBuffer desc_buffer;

  TB_DYN_ARR_OF(TbMaterialDomainHandler) usage_map;
} TbMaterialCtx;
ECS_COMPONENT_DECLARE(TbMaterialCtx);

ECS_COMPONENT_DECLARE(TbMaterialComponent);

// Describes the creation of a material that lives in a GLB file
typedef struct TbMaterialGLTFLoadRequest {
  const cgltf_data *data;
  const char *name;
} TbMaterialGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbMaterialGLTFLoadRequest);

ECS_TAG_DECLARE(TbMaterialLoaded);
ECS_TAG_DECLARE(TbMaterialUploaded);

// Internals

typedef struct TbMaterialLoadedArgs {
  ecs_world_t *ecs;
  TbMaterial2 mat;
  TbMaterialDomain domain;
  TbMaterialData comp;
} TbMaterialLoadedArgs;

void tb_material_loaded(const void *args) {
  tb_auto loaded_args = (const TbMaterialLoadedArgs *)args;
  tb_auto ecs = loaded_args->ecs;
  tb_auto mat = loaded_args->mat;
  if (mat == 0) {
    TB_CHECK(false, "Material load failed. Do we need to retry?");
  }

  loaded_args->domain.load_fn(ecs, loaded_args->comp.domain_data);

  SDL_AtomicDecRef(&tb_parallel_mat_load_count);
  ecs_add(ecs, mat, TbMaterialLoaded);
  ecs_set_ptr(ecs, mat, TbMaterialData, &loaded_args->comp);
}

typedef struct TbLoadCommonMaterialArgs {
  ecs_world_t *ecs;
  TbMaterial2 mat;
  TbTaskScheduler enki;
  TbPinnedTask loaded_task;
  TbMaterialDomain domain;
} TbLoadCommonMaterialArgs;

typedef struct TbLoadGLTFMaterialArgs {
  TbLoadCommonMaterialArgs common;
  TbMaterialGLTFLoadRequest gltf;
} TbLoadGLTFMaterialArgs;

TbMaterialData tb_parse_gltf_mat(const cgltf_data *gltf_data, const char *name,
                                 TbMatParseFn parse_fn,
                                 const cgltf_material *material) {
  TracyCZoneN(ctx, "Load Material", true);

  TbMaterialData mat_data = {0};

  // Load material based on usage
  void *data = NULL;
  if (!parse_fn(gltf_data, name, material, &data)) {
    tb_free(tb_global_alloc, data);
    TracyCZoneEnd(ctx);
    return mat_data;
  }

  mat_data.domain_data = data;

  TracyCZoneEnd(ctx);
  return mat_data;
}

void tb_load_gltf_material_task(const void *args) {
  TracyCZoneN(ctx, "Load GLTF Material Task", true);
  tb_auto load_args = (const TbLoadGLTFMaterialArgs *)args;
  TbMaterial2 mat = load_args->common.mat;
  tb_auto domain = load_args->common.domain;

  tb_auto data = load_args->gltf.data;
  tb_auto name = load_args->gltf.name;

  // Find material by name
  struct cgltf_material *material = NULL;
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    tb_auto m = &data->materials[i];
    if (SDL_strcmp(name, m->name) == 0) {
      material = m;
      break;
    }
  }
  if (material == NULL) {
    TB_CHECK(false, "Failed to find material by name");
    mat = 0; // Invalid ent means task failed
  }

  // Parse material based on usage
  TbMaterialData mat_data = {0};
  if (mat != 0) {
    mat_data = tb_parse_gltf_mat(data, name, domain.parse_fn, material);
  }

  tb_free(tb_global_alloc, (void *)name);

  // Launch pinned task to handle loading signals on main thread
  TbMaterialLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .mat = mat,
      .comp = mat_data,
      .domain = domain,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbMaterialLoadedArgs));
  TracyCZoneEnd(ctx);
}

TbMaterialDomainHandler tb_find_material_domain(const TbMaterialCtx *ctx,
                                                TbMaterialUsage usage) {
  TB_TRACY_SCOPE("Find Mat Domain");
  TB_DYN_ARR_FOREACH(ctx->usage_map, i) {
    tb_auto handler = &TB_DYN_ARR_AT(ctx->usage_map, i);
    if (handler->usage == usage) {
      return *handler;
    }
  }
  TB_CHECK(false, "Failed to find material domain from usage");
  return (TbMaterialDomainHandler){0};
}

// Systems

void tb_queue_gltf_mat_loads(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Queue GLTF Tex Loads", true);

  tb_auto ecs = it->world;

  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 2);
  tb_auto reqs = ecs_field(it, TbMaterialGLTFLoadRequest, 3);
  tb_auto usages = ecs_field(it, TbMaterialUsage, 4);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(&tb_parallel_mat_load_count) >
        TbMaxParallelMaterialLoads) {
      break;
    }
    TbMaterial2 ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    TbMaterialDomainHandler handler = tb_find_material_domain(mat_ctx, usage);
    if (handler.type_size == 0 || handler.usage == TB_MAT_USAGE_UNKNOWN) {
      TB_CHECK(false, "Unexpected material usage");
    }

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_material_loaded, NULL, 0);

    TbLoadGLTFMaterialArgs args = {
        .common =
            {
                .ecs = ecs,
                .mat = ent,
                .enki = enki,
                .loaded_task = loaded_task,
                .domain = handler.domain,
            },
        .gltf = req,
    };
    TbTask load_task = tb_async_task(enki, tb_load_gltf_material_task, &args,
                                     sizeof(TbLoadGLTFMaterialArgs));
    // Apply task component to texture entity
    ecs_set(ecs, ent, TbTask, {load_task});

    SDL_AtomicIncRef(&tb_parallel_mat_load_count);
    mat_ctx->owned_mat_count++;

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(ecs, ent, TbMaterialGLTFLoadRequest);
  }
  TracyCZoneEnd(ctx);
}

void tb_upload_gltf_mats(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Material Uploads");
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 1);
  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 2);

  tb_auto materials = ecs_field(it, TbMaterialData, 3);
  tb_auto usages = ecs_field(it, TbMaterialUsage, 4);
  for (int32_t i = 0; i < it->count; ++i) {
    TbMaterial2 ent = it->entities[i];
    tb_auto material = &materials[i];
    tb_auto usage = usages[i];

    const char *name = ecs_get_name(it->world, ent);

    // Determine if the material's dependencies are also met
    tb_auto handler = tb_find_material_domain(mat_ctx, usage);
    tb_auto domain = handler.domain;

    // Material must be skipped if its dependencies aren't ready
    if (!domain.ready_fn(it->world, material)) {
      continue;
    }

    void *domain_data = domain.get_data_fn(it->world, material);
    size_t domain_size = domain.get_size_fn();

    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = domain_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    // HACK: Known alignment for uniform buffers
    tb_rnd_sys_create_gpu_buffer2_tmp(rnd_sys, &create_info, domain_data, name,
                                      &material->gpu_buffer, 0x40);

    ecs_remove(it->world, ent, TbMaterialLoaded);
    ecs_add(it->world, ent, TbMaterialUploaded);
    tb_rnd_reset_descriptor_count(it->world, ent);
  }
}

void tb_finalize_materials(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Finalize Materials");

  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 1);
  tb_auto materials = ecs_field(it, TbMaterialData, 2);
  tb_auto world = ecs_singleton_get(it->world, TbWorldRef)->world;

  uint64_t mat_count = mat_ctx->owned_mat_count;

  if (mat_count == 0) {
    return;
  }

  // Collect a write for every new material
  TB_DYN_ARR_OF(TbDynDescWrite) writes = {0};
  TB_DYN_ARR_RESET(writes, world->gp_alloc, 16);
  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto material = &materials[i];
    TB_CHECK(material->gpu_buffer.info.size, "Material GPU Buffer is size 0");
    TbDynDescWrite write = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .desc.buffer =
            (VkDescriptorBufferInfo){
                .buffer = material->gpu_buffer.buffer,
                .range = material->gpu_buffer.info.size,
            },
    };
    TB_DYN_ARR_APPEND(writes, write);
  }

  const tb_auto write_count = TB_DYN_ARR_SIZE(writes);
  if (write_count > 0) {
    // Allocate space for indices
    uint32_t *mat_indices =
        tb_alloc_nm_tp(world->tmp_alloc, TB_DYN_ARR_SIZE(writes), uint32_t);
    tb_write_dyn_desc_pool(&mat_ctx->desc_pool, write_count, writes.data,
                           mat_indices);

    TB_DYN_ARR_FOREACH(writes, i) {
      // Material is now ready to be referenced elsewhere
      ecs_set(it->world, it->entities[i], TbMaterialComponent,
              {mat_indices[i]});
      tb_rnd_mark_descriptor(it->world, it->entities[i]);
    }
  }

  TB_DYN_ARR_DESTROY(writes);
}

void tb_update_material_pool(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Update Material Pool");
  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  if (mat_ctx->owned_mat_count == 0) {
    return;
  }
  // Tick the pool in case any new materials require us to expand the pool
  tb_tick_dyn_desc_pool(rnd_sys, &mat_ctx->desc_pool, "material");
}

// Toybox Glue

void tb_register_material2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMaterialCtx);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialComponent);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialData);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialDomainHandler);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialUsage);
  ECS_TAG_DEFINE(ecs, TbMaterialLoaded);
  ECS_TAG_DEFINE(ecs, TbMaterialUploaded);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  ECS_SYSTEM(ecs, tb_queue_gltf_mat_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbMaterialCtx(TbMaterialCtx),
             [in] TbMaterialGLTFLoadRequest, [in] TbMaterialUsage);

  ECS_SYSTEM(ecs, tb_upload_gltf_mats, EcsPreUpdate,
             TbRenderSystem(TbRenderSystem), TbMaterialCtx(TbMaterialCtx),
             [in] TbMaterialData, [in] TbMaterialUsage, [in] TbMaterialLoaded,
             !TbMaterialUploaded);

  ECS_SYSTEM(ecs, tb_finalize_materials,
             EcsPreStore, [in] TbMaterialCtx(TbMaterialCtx),
             [in] TbMaterialData, [in] TbMaterialUploaded,
             TbNeedsDescriptorUpdate, !TbUpdatingDescriptor,
             !TbDescriptorReady);
  ECS_SYSTEM(ecs, tb_update_material_pool,
             EcsPostUpdate, [in] TbMaterialCtx(TbMaterialCtx),
             [in] TbRenderSystem(TbRenderSystem));

  TbMaterialCtx ctx = {0};

  SDL_AtomicSet(&tb_parallel_mat_load_count, 0);

  // Create immutable sampler for materials
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f, // 16x anisotropy is cheap
        .maxLod = 14.0f,        // HACK: known number of mips for 8k textures
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    tb_rnd_create_sampler(rnd_sys, &create_info, "Material Sampler",
                          &ctx.sampler);
  }

  // Create immutable sampler for sampling shadows
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .maxAnisotropy = 1.0f,
        .maxLod = 1.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    tb_rnd_create_sampler(rnd_sys, &create_info, "Material Shadow Sampler",
                          &ctx.shadow_sampler);
  }

  // Create descriptor set layout for materials
  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    const uint32_t binding_count = 3;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = binding_count,
                .pBindingFlags =
                    (VkDescriptorBindingFlags[binding_count]){0, 0, flags},
            },
        .bindingCount = binding_count,
        .pBindings =
            (VkDescriptorSetLayoutBinding[binding_count]){
                {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &ctx.sampler},
                {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &ctx.shadow_sampler},
                {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 2048, // HACK: Some high upper limit
                 VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                 NULL},
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Material Set Layout",
                             &ctx.set_layout);
  }
#if TB_USE_DESC_BUFFER == 1
  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    const uint32_t binding_count = 3;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = binding_count,
                .pBindingFlags =
                    (VkDescriptorBindingFlags[binding_count]){0, 0, flags},
            },
        .bindingCount = binding_count,
        .pBindings =
            (VkDescriptorSetLayoutBinding[binding_count]){
                {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &ctx.sampler},
                {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &ctx.shadow_sampler},
                {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 2048, // HACK: Some high upper limit
                 VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                 NULL},
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Material Set Layout2",
                             &ctx.set_layout2);
    tb_create_descriptor_buffer(rnd_sys, ctx.set_layout2,
                                "Material Descriptors", 4, &ctx.desc_buffer);
  }
#endif

  TB_DYN_ARR_RESET(ctx.usage_map, tb_global_alloc, 4);

  tb_create_dyn_desc_pool(rnd_sys, ctx.set_layout,
                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ctx.desc_pool, 2);

  // Must set ctx before we try to load any materials
  ecs_singleton_set_ptr(ecs, TbMaterialCtx, &ctx);

  // Register default material usage handlers
  tb_register_scene_material_domain(ecs);
}

void tb_unregister_material2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);
  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout2);

  tb_destroy_descriptor_buffer(rnd_sys, &ctx->desc_buffer);

  // TODO: Release all default references

  // TODO: Check for leaks

  // TODO: Clean up descriptor pool

  ecs_singleton_remove(ecs, TbMaterialCtx);
}

TB_REGISTER_SYS(tb, material2, TB_MAT_SYS_PRIO)

// Public API

bool tb_register_mat_usage(ecs_world_t *ecs, const char *domain_name,
                           TbMaterialUsage usage, TbMaterialDomain domain,
                           void *default_data, size_t size) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  // Copy data onto the global allocator so it can be safely freed
  // from a thread
  uint8_t *data_copy = tb_alloc_nm_tp(tb_global_alloc, size, uint8_t);
  SDL_memcpy(data_copy, default_data, size);

  TbMaterial2 default_mat = ecs_new_entity(ecs, 0);
  ecs_set(ecs, default_mat, TbMaterialUsage, {usage});
  ecs_add(ecs, default_mat, TbMaterialLoaded);

  TbMaterialData mat_data = {
      .domain_data = data_copy,
  };

  const size_t data_size = domain.get_size_fn();

  const uint32_t name_max = 100;
  char name[name_max] = {0};
  SDL_snprintf(name, name_max, "%s_default", domain_name);

  VkBufferCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = data_size,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
  };
  // HACK: Known alignment for uniform buffers
  tb_rnd_sys_create_gpu_buffer2_tmp(rnd_sys, &create_info, data_copy, name,
                                    &mat_data.gpu_buffer, 0x40);

  ecs_set_ptr(ecs, default_mat, TbMaterialData, &mat_data);

  TbMaterialDomainHandler handler = {
      .usage = usage,
      .domain = domain,
      .type_size = size,
      .default_mat = default_mat,
  };
  TB_DYN_ARR_APPEND(ctx->usage_map, handler);

  ctx->owned_mat_count++;
  return true;
}

VkDescriptorSetLayout tb_mat_sys_get_set_layout(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);
#if TB_USE_DESC_BUFFER == 1
  return ctx->set_layout2;
#else
  return ctx->set_layout;
#endif
}

VkDescriptorSet tb_mat_sys_get_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->desc_pool);
}

VkDescriptorBufferBindingInfoEXT tb_mat_sys_get_table_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);
  return tb_desc_buff_get_binding(&ctx->desc_buffer);
}

TbMaterial2 tb_mat_sys_load_gltf_mat(ecs_world_t *ecs, const cgltf_data *data,
                                     const char *name, TbMaterialUsage usage) {
  /*
    If we are in a deferred ecs state (in the middle of the execution of a
  system) we would not be able to determine if a material entity already exists
  or not. So the calling system *must* be no_readonly and we will have to
  manually check if we need to stop suspending commands.
  Otherwise a system that attempts to add the same material twice will not be
  looking at the correct version of the ECS state when trying to determine if an
  entity for a material already exists
  */
  bool deferred = false;
  if (ecs_is_deferred(ecs)) {
    deferred = ecs_defer_end(ecs);
  }
  // If an entity already exists with this name it is either loading or loaded
  TbMaterial2 mat_ent = ecs_lookup_child(ecs, ecs_id(TbMaterialCtx), name);
  if (mat_ent != 0) {
    if (deferred) {
      ecs_defer_begin(ecs);
    }
    return mat_ent;
  }

  if (data == NULL) {
    TB_CHECK(false, "Expected data");
    return 0;
  }

  // Create a material entity
  mat_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, mat_ent, name);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, mat_ent, EcsChildOf, ecs_id(TbMaterialCtx));

  // Need to copy strings for task safety
  // Task is responsible for freeing this string
  const size_t name_len = SDL_strnlen(name, 256) + 1;
  char *name_cpy = tb_alloc_nm_tp(tb_global_alloc, name_len, char);
  SDL_strlcpy(name_cpy, name, name_len);

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, mat_ent, TbMaterialGLTFLoadRequest, {data, name_cpy});
  ecs_set(ecs, mat_ent, TbMaterialUsage, {usage});
  tb_rnd_reset_descriptor_count(ecs, mat_ent);

  if (deferred) {
    ecs_defer_begin(ecs);
  }

  return mat_ent;
}

bool tb_is_material_ready(ecs_world_t *ecs, TbMaterial2 mat_ent) {
  return ecs_has(ecs, mat_ent, TbMaterialUploaded) &&
         ecs_has(ecs, mat_ent, TbMaterialComponent) &&
         ecs_has(ecs, mat_ent, TbDescriptorReady);
}

bool tb_is_mat_transparent(ecs_world_t *ecs, TbMaterial2 mat_ent) {
  tb_auto ctx = ecs_singleton_get(ecs, TbMaterialCtx);
  tb_auto data = ecs_get(ecs, mat_ent, TbMaterialData);
  tb_auto usage = *ecs_get(ecs, mat_ent, TbMaterialUsage);

  tb_auto handler = tb_find_material_domain(ctx, usage);
  return handler.domain.is_trans_fn(data);
}

TbMaterial2 tb_get_default_mat(ecs_world_t *ecs, TbMaterialUsage usage) {
  tb_auto ctx = ecs_singleton_get(ecs, TbMaterialCtx);
  TB_DYN_ARR_FOREACH(ctx->usage_map, i) {
    tb_auto handler = &TB_DYN_ARR_AT(ctx->usage_map, i);
    if (handler->usage == usage) {
      return handler->default_mat;
    }
  }
  TB_CHECK(false, "Failed to get default material");
  return 0;
}
