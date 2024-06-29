#include "tb_material_system.h"

#include "tb_assets.h"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_queue.h"
#include "tb_scene_material.h"
#include "tb_task_scheduler.h"
#include "tb_world.h"

static const int32_t TbMaxParallelMaterialLoads = 8;

ECS_TAG_DECLARE(TbMatLoadPhaseLoading);
ECS_TAG_DECLARE(TbMatLoadPhaseLoaded);
ECS_TAG_DECLARE(TbMatLoadPhaseWriting);
ECS_TAG_DECLARE(TbMatLoadPhaseWritten);
ECS_TAG_DECLARE(TbMatLoadPhaseReady);

// Components

ECS_COMPONENT_DECLARE(TbMaterialUsage);

typedef SDL_AtomicInt TbMatQueueCounter;
ECS_COMPONENT_DECLARE(TbMatQueueCounter);

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
  TbFrameDescriptorPoolList frame_set_pool;
  uint32_t owned_mat_count;

  uint32_t pool_update_counter;
  uint32_t last_written_mat_count;

  ecs_query_t *uploadable_mat_query;
  ecs_query_t *loaded_mat_query;
  ecs_query_t *dirty_mat_query;
  ecs_query_t *ready_mat_query;

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

ECS_TAG_DECLARE(TbMaterialUploadable);
ECS_TAG_DECLARE(TbMaterialLoaded);

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

  ecs_add(ecs, mat, TbMaterialUploadable);
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
  tb_auto counter = ecs_field(it, TbMatQueueCounter, 2);
  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 3);
  tb_auto reqs = ecs_field(it, TbMaterialGLTFLoadRequest, 4);
  tb_auto usages = ecs_field(it, TbMaterialUsage, 5);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelMaterialLoads) {
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

    SDL_AtomicIncRef(counter);
    mat_ctx->owned_mat_count++;
    tb_mat_sys_begin_load(ecs);

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(ecs, ent, TbMaterialGLTFLoadRequest);
  }
  TracyCZoneEnd(ctx);
}

void tb_upload_gltf_mats(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Material Uploads", true);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 1);
  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 2);

  ecs_iter_t mat_it = ecs_query_iter(it->world, mat_ctx->uploadable_mat_query);
  while (ecs_query_next(&mat_it)) {
    tb_auto materials = ecs_field(&mat_it, TbMaterialData, 1);
    tb_auto usages = ecs_field(&mat_it, TbMaterialUsage, 2);
    for (int32_t i = 0; i < mat_it.count; ++i) {
      TbMaterial2 ent = mat_it.entities[i];
      tb_auto material = &materials[i];
      tb_auto usage = usages[i];

      const char *name = ecs_get_name(mat_it.world, ent);

      // Determine if the material's dependencies are also met
      tb_auto handler = tb_find_material_domain(mat_ctx, usage);
      tb_auto domain = handler.domain;

      // Material must be skipped if its dependencies aren't ready
      if (!domain.ready_fn(mat_it.world, material)) {
        continue;
      }

      void *domain_data = domain.get_data_fn(mat_it.world, material);
      size_t domain_size = domain.get_size_fn();

      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = domain_size,
          .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      };
      // HACK: Known alignment for uniform buffers
      tb_rnd_sys_create_gpu_buffer2_tmp(rnd_sys, &create_info, domain_data,
                                        name, &material->gpu_buffer, 0x40);

      ecs_remove(mat_it.world, ent, TbMaterialUploadable);
      ecs_add(mat_it.world, ent, TbMaterialLoaded);
      TB_LOG_DEBUG(SDL_LOG_CATEGORY_APPLICATION, "Loaded Material %d", ent);
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_reset_mat_queue_count(ecs_iter_t *it) {
  tb_auto counter = ecs_field(it, TbMatQueueCounter, 1);
  SDL_AtomicSet(counter, 0);
}

void tb_mat_phase_loading(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Material Loading State", true);

  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 1);

  if (mat_ctx->owned_mat_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  uint64_t loaded_mat_count = 0;
  ecs_iter_t mat_it = ecs_query_iter(it->world, mat_ctx->loaded_mat_query);
  while (ecs_query_next(&mat_it)) {
    loaded_mat_count += mat_it.count;
  }

  // We are in the loading phase and all materials have loaded
  // Move on to the next phase
  if (mat_ctx->owned_mat_count == loaded_mat_count) {
    ecs_remove(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseLoading);
    ecs_add(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseLoaded);
    mat_ctx->pool_update_counter = 0;
    mat_ctx->last_written_mat_count = loaded_mat_count;
  }

  TracyCZoneEnd(ctx);
}

void tb_update_material_pool(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Update Material Descriptors", true);

  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);

  uint64_t mat_count = 0;

  // Accumulate the number of materials
  ecs_iter_t mat_it = ecs_query_iter(it->world, mat_ctx->loaded_mat_query);
  while (ecs_query_next(&mat_it)) {
    mat_count += mat_it.count;
  }

  if (mat_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Resize the pool if necessary
  if (mat_count != tb_rnd_frame_desc_pool_get_desc_count(
                       rnd_sys, mat_ctx->frame_set_pool.pools)) {

    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 4,
                },
            },
    };
    VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = (uint32_t[1]){mat_count},
    };
    tb_rnd_frame_desc_pool_tick(rnd_sys, "material", &create_info,
                                &mat_ctx->set_layout, &alloc_info,
                                mat_ctx->frame_set_pool.pools, 1, mat_count);
  }

  // When we get to this phase all materials must be marked dirty
  mat_it = ecs_query_iter(it->world, mat_ctx->loaded_mat_query);
  while (ecs_query_next(&mat_it)) {
    tb_auto ecs = mat_it.world;
    for (int32_t i = 0; i < mat_it.count; ++i) {
      tb_auto mat_ent = mat_it.entities[i];
      ecs_add(ecs, mat_ent, TbNeedsDescriptorUpdate);
      ecs_remove(ecs, mat_ent, TbDescriptorReady);
      if (!ecs_has(ecs, mat_ent, TbDescriptorCounter)) {
        ecs_set(ecs, mat_ent, TbDescriptorCounter, {0});
      } else {
        tb_auto counter = ecs_get_mut(ecs, mat_ent, TbDescriptorCounter);
        SDL_AtomicSet(counter, 0);
      }
    }
  }

  mat_ctx->pool_update_counter++;

  // One pool must be resized per frame
  if (mat_ctx->pool_update_counter >= TB_MAX_FRAME_STATES) {
    ecs_remove(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseLoaded);
    ecs_add(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseWriting);

    // Reset counter for next phase
    mat_ctx->pool_update_counter = 0;
  }

  TracyCZoneEnd(ctx);
}

void tb_write_material_descriptors(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Write Material Descriptors", true);

  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto world = ecs_singleton_get(it->world, TbWorldRef)->world;

  uint64_t mat_count = 0;

  // Accumulate the number of textures
  ecs_iter_t mat_it = ecs_query_iter(it->world, mat_ctx->dirty_mat_query);
  while (ecs_query_next(&mat_it)) {
    mat_count += mat_it.count;
  }

  if (mat_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Write all dirty materials into the descriptor set table
  uint32_t mat_idx = 0;
  mat_it = ecs_query_iter(it->world, mat_ctx->dirty_mat_query);

  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  TB_DYN_ARR_OF(VkDescriptorBufferInfo) buf_info = {0};
  TB_DYN_ARR_RESET(writes, world->tmp_alloc, mat_count);
  TB_DYN_ARR_RESET(buf_info, world->tmp_alloc, mat_count);
  while (ecs_query_next(&mat_it)) {
    tb_auto materials = ecs_field(&mat_it, TbMaterialData, 1);
    tb_auto mat_usages = ecs_field(&mat_it, TbMaterialUsage, 2);
    for (int32_t i = 0; i < mat_it.count; ++i) {
      tb_auto material = &materials[i];
      tb_auto usage = mat_usages[i];

      // Get material domain handler
      tb_auto mat_domain = tb_find_material_domain(mat_ctx, usage).domain;

      // If a material isn't ready we mustn't exit this phase
      if (!mat_domain.ready_fn(it->world, material)) {
        TracyCZoneEnd(ctx);
        return;
      }

      VkDescriptorBufferInfo buffer_info = {
          .range = VK_WHOLE_SIZE,
          .buffer = material->gpu_buffer.buffer,
      };
      TB_DYN_ARR_APPEND(buf_info, buffer_info);

      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .dstSet = tb_rnd_frame_desc_pool_get_set(
              rnd_sys, mat_ctx->frame_set_pool.pools, 0),
          .dstArrayElement = mat_idx,
          .dstBinding = 2,
          .pBufferInfo = &TB_DYN_ARR_AT(buf_info, mat_idx),
      };
      TB_DYN_ARR_APPEND(writes, write);

      // Material is now ready to be referenced elsewhere
      ecs_set(it->world, mat_it.entities[i], TbMaterialComponent, {mat_idx});
      ecs_add(it->world, mat_it.entities[i], TbUpdatingDescriptor);
      mat_idx++;
    }
  }
  tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);

  mat_ctx->pool_update_counter++;

  // One write must be enqueued per frame
  if (mat_ctx->pool_update_counter >= TB_MAX_FRAME_STATES) {
    ecs_remove(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseWriting);
    ecs_add(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseWritten);
  }

  TracyCZoneEnd(ctx);
}

void tb_mat_phase_written(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Material Written Phase", true);

  tb_auto mat_ctx = ecs_field(it, TbMaterialCtx, 1);

  uint64_t total_mat_count = mat_ctx->owned_mat_count;

  uint64_t ready_mat_count = 0;
  ecs_iter_t mat_it = ecs_query_iter(it->world, mat_ctx->ready_mat_query);
  while (ecs_query_next(&mat_it)) {
    ready_mat_count += mat_it.count;
  }

  // When all textures are ready we can consider this phase over
  if (total_mat_count > 0 && total_mat_count == ready_mat_count) {
    ecs_remove(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseWritten);
    ecs_add(it->world, ecs_id(TbMaterialCtx), TbMatLoadPhaseReady);
  }

  TracyCZoneEnd(ctx);
}

// Toybox Glue

void tb_register_material2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMaterialCtx);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialComponent);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialData);
  ECS_COMPONENT_DEFINE(ecs, TbMatQueueCounter);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialDomainHandler);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialUsage);
  ECS_TAG_DEFINE(ecs, TbMaterialUploadable);
  ECS_TAG_DEFINE(ecs, TbMaterialLoaded);
  ECS_TAG_DEFINE(ecs, TbMatLoadPhaseLoading);
  ECS_TAG_DEFINE(ecs, TbMatLoadPhaseLoaded);
  ECS_TAG_DEFINE(ecs, TbMatLoadPhaseWriting);
  ECS_TAG_DEFINE(ecs, TbMatLoadPhaseWritten);
  ECS_TAG_DEFINE(ecs, TbMatLoadPhaseReady);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  ECS_SYSTEM(ecs, tb_queue_gltf_mat_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler),
             TbMatQueueCounter(TbMatQueueCounter), TbMaterialCtx(TbMaterialCtx),
             [in] TbMaterialGLTFLoadRequest, [in] TbMaterialUsage);

  ECS_SYSTEM(ecs, tb_upload_gltf_mats, EcsPreUpdate,
             TbRenderSystem(TbRenderSystem), TbMaterialCtx(TbMaterialCtx));

  ECS_SYSTEM(ecs, tb_reset_mat_queue_count,
             EcsPostUpdate, [in] TbMatQueueCounter(TbMatQueueCounter));

  ECS_SYSTEM(ecs, tb_mat_phase_loading, EcsPreFrame,
             TbMaterialCtx(TbMaterialCtx), TbMatLoadPhaseLoading);
  ECS_SYSTEM(ecs, tb_update_material_pool,
             EcsPostUpdate, [in] TbMaterialCtx(TbMaterialCtx),
             [in] TbRenderSystem(TbRenderSystem), TbMatLoadPhaseLoaded);
  ECS_SYSTEM(ecs, tb_write_material_descriptors,
             EcsPreStore, [in] TbMaterialCtx(TbMaterialCtx),
             [in] TbRenderSystem(TbRenderSystem), TbMatLoadPhaseWriting);
  ECS_SYSTEM(ecs, tb_mat_phase_written, EcsOnStore,
             TbMaterialCtx(TbMaterialCtx), TbMatLoadPhaseWritten);

  TbMaterialCtx ctx = {
      .owned_mat_count = 0,
      .uploadable_mat_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbMaterialData), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialUsage), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialUploadable), .inout = EcsIn},
                    }}),
      .loaded_mat_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbMaterialData), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialUsage), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialLoaded)},
                    }}),
      .dirty_mat_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbMaterialData), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialUsage), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialLoaded)},
                        {.id = ecs_id(TbNeedsDescriptorUpdate)},
                    }}),
      .ready_mat_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbMaterialData), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialUsage), .inout = EcsIn},
                        {.id = ecs_id(TbMaterialLoaded)},
                        {.id = ecs_id(TbDescriptorReady)},
                    }}),
  };

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

  TbMatQueueCounter queue_count = {0};
  SDL_AtomicSet(&queue_count, 0);
  ecs_singleton_set_ptr(ecs, TbMatQueueCounter, &queue_count);

  TB_DYN_ARR_RESET(ctx.usage_map, tb_global_alloc, 4);

  // Must set ctx before we try to load any materials
  ecs_singleton_set_ptr(ecs, TbMaterialCtx, &ctx);

  // Register default material usage handlers
  tb_register_scene_material_domain(ecs);
}

void tb_unregister_material2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);

  ecs_query_fini(ctx->uploadable_mat_query);
  ecs_query_fini(ctx->loaded_mat_query);
  ecs_query_fini(ctx->dirty_mat_query);
  ecs_query_fini(ctx->ready_mat_query);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);

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
  ecs_add(ecs, default_mat, TbMaterialUploadable);

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
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
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
  return ctx->set_layout;
}

VkDescriptorSet tb_mat_sys_get_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_rnd_frame_desc_pool_get_set(rnd_sys, ctx->frame_set_pool.pools, 0);
}

void tb_mat_sys_begin_load(ecs_world_t *ecs) {
  if (!ecs_has(ecs, ecs_id(TbMaterialCtx), TbMatLoadPhaseLoading)) {
    ecs_remove(ecs, ecs_id(TbMaterialCtx), TbMatLoadPhaseLoaded);
    ecs_remove(ecs, ecs_id(TbMaterialCtx), TbMatLoadPhaseWriting);
    ecs_remove(ecs, ecs_id(TbMaterialCtx), TbMatLoadPhaseWritten);
    ecs_remove(ecs, ecs_id(TbMaterialCtx), TbMatLoadPhaseReady);
    ecs_add(ecs, ecs_id(TbMaterialCtx), TbMatLoadPhaseLoading);
  }
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
  ecs_add(ecs, mat_ent, TbNeedsDescriptorUpdate);

  if (deferred) {
    ecs_defer_begin(ecs);
  }

  return mat_ent;
}

bool tb_is_material_ready(ecs_world_t *ecs, TbMaterial2 mat_ent) {
  return ecs_has(ecs, mat_ent, TbMaterialLoaded) &&
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
