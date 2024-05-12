#include "tb_texture_system.h"

#include "assets.h"
#include "tb_task_scheduler.h"
#include "tbgltf.h"
#include "tbktx.h"
#include "tbqueue.h"
#include "world.h"

ECS_COMPONENT_DECLARE(TbTextureUsage);

typedef struct TbTexture2Ctx {
  VkDescriptorSetLayout set_layout;
  TbDescriptorPool set_pool;

  TbTexture2 default_color_tex;
  TbTexture2 default_normal_tex;
  TbTexture2 default_metal_rough_tex;
  TbTexture2 brdf_tex;
} TbTexture2Ctx;
ECS_COMPONENT_DECLARE(TbTexture2Ctx);

typedef struct TbTextureImage {
  uint32_t ref_count;
  TbHostBuffer host_buffer;
  TbImage gpu_image;
  VkImageView image_view;
} TbTextureImage;
ECS_COMPONENT_DECLARE(TbTextureImage);

typedef uint32_t TbTextureComponent2;
ECS_COMPONENT_DECLARE(TbTextureComponent2);

// Describes the creation of a texture that lives in a GLB file
typedef struct TbTextureGLTFLoadRequest {
  const char *path;
  const char *mat_name; // TODO: Should be an entity id
} TbTextureGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbTextureGLTFLoadRequest);

typedef struct TbTextureKTXLoadRequest {
  const char *path;
  const char *name;
} TbTextureKTXLoadRequest;
ECS_COMPONENT_DECLARE(TbTextureKTXLoadRequest);

typedef struct TbTextureRawLoadRequest {
  const char *name;
  const uint8_t *pixels;
  uint64_t size;
  uint32_t width;
  uint32_t height;
} TbTextureRawLoadRequest;
ECS_COMPONENT_DECLARE(TbTextureRawLoadRequest);

ECS_TAG_DECLARE(TbTextureLoaded);
ECS_TAG_DECLARE(TbNeedTexDescUpdate);

// Internals

// Some goodies from the old texture system
typedef struct KTX2IterData {
  VkBuffer buffer;
  VkImage image;
  TbBufferImageCopy *uploads;
  uint64_t offset;
} KTX2IterData;
extern ktx_error_code_e iterate_ktx2_levels(int32_t mip_level, int32_t face,
                                            int32_t width, int32_t height,
                                            int32_t depth,
                                            uint64_t face_lod_size,
                                            void *pixels, void *userdata);
extern VkImageType get_ktx2_image_type(const ktxTexture2 *t);
extern VkImageViewType get_ktx2_image_view_type(const ktxTexture2 *t);

TbTextureImage tb_load_ktx_image(TbRenderSystem *rnd_sys, const char *name,
                                 ktxTexture2 *ktx) {
  bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
  if (needs_transcoding) {
    // TODO: pre-calculate the best format for the platform
    ktx_error_code_e err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
    TB_CHECK(err == KTX_SUCCESS, "Failed to transcode basis texture");
  }

  size_t host_buffer_size = ktx->dataSize;
  uint32_t width = ktx->baseWidth;
  uint32_t height = ktx->baseHeight;
  uint32_t depth = ktx->baseDepth;
  uint32_t layers = ktx->numLayers;
  uint32_t mip_levels = ktx->numLevels;
  VkFormat format = (VkFormat)ktx->vkFormat;

  TB_CHECK(ktx->generateMipmaps == false,
           "Not expecting to have to generate mips");

  TbTextureImage texture = {0};

  // Allocate gpu image
  {
    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .arrayLayers = layers,
        .extent =
            (VkExtent3D){
                .width = width,
                .height = height,
                .depth = depth,
            },
        .format = format,
        .imageType = get_ktx2_image_type(ktx),
        .mipLevels = mip_levels,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    tb_rnd_sys_create_gpu_image(rnd_sys, ktx->pData, host_buffer_size,
                                &create_info, name, &texture.gpu_image,
                                &texture.host_buffer);
  }

  // Create image view
  {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.gpu_image.image,
        .viewType = get_ktx2_image_view_type(ktx),
        .format = format,
        .components =
            {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange =
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                mip_levels,
                0,
                layers,
            },
    };
    char view_name[100] = {0};
    SDL_snprintf(view_name, 100, "%s Image TbView", name); // NOLINT
    tb_rnd_create_image_view(rnd_sys, &create_info, view_name,
                             &texture.image_view);
  }

  // Issue uploads
  {
    TbBufferImageCopy *uploads =
        tb_alloc_nm_tp(rnd_sys->tmp_alloc, mip_levels, TbBufferImageCopy);

    KTX2IterData iter_data = {
        .buffer = texture.host_buffer.buffer,
        .image = texture.gpu_image.image,
        .offset = texture.host_buffer.offset,
        .uploads = uploads,
    };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
    // Iterate over texture levels to fill out upload requests
    ktxTexture_IterateLevels(ktx, iterate_ktx2_levels, &iter_data);
#pragma clang diagnostic pop

    // Will handle transitioning the image's layout to shader read only
    tb_rnd_upload_buffer_to_image(rnd_sys, uploads, mip_levels);
  }

  return texture;
}

TbTextureImage tb_load_gltf_texture(TbRenderSystem *rnd_sys, const char *name,
                                    const cgltf_texture *texture) {
  TracyCZoneN(ctx, "Load Texture2", true);
  TbTextureImage tex = {0};

  if (texture->has_basisu) {
    const cgltf_image *image = texture->basisu_image;

    const cgltf_buffer_view *image_view = image->buffer_view;
    TB_CHECK(image->buffer_view->buffer->uri == NULL,
             "Not setup to load data from uri");
    const cgltf_buffer *image_data = image_view->buffer;

    // Points to some jpg/png whatever image format data
    uint8_t *raw_data = (uint8_t *)(image_data->data) + image_view->offset;
    const int32_t raw_size = (int32_t)image_view->size;

    // Parse the ktx texture
    ktxTexture2 *ktx = NULL;
    {
      ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

      ktx_error_code_e err =
          ktxTexture2_CreateFromMemory(raw_data, raw_size, flags, &ktx);
      TB_CHECK(err == KTX_SUCCESS, "Failed to create KTX texture from memory");
    }
    tex = tb_load_ktx_image(rnd_sys, name, ktx);
  } else {
    TB_CHECK(false, "Uncompressed texture loading not implemented");
  }

  TracyCZoneEnd(ctx);
  return tex;
}

TbTextureImage tb_load_raw_image(TbRenderSystem *rnd_sys, const char *name,
                                 const uint8_t *pixels, uint64_t size,
                                 uint32_t width, uint32_t height,
                                 TbTextureUsage usage) {
  TracyCZoneN(ctx, "Load Raw Texture2", true);

  TbTextureImage texture = {0};

  // Determine some parameters
  const uint32_t depth = 1;
  const uint32_t layers = 1;
  const uint32_t mip_levels = 1;
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  if (usage == TB_TEX_USAGE_COLOR) {
    format = VK_FORMAT_R8G8B8A8_SRGB;
  } else if (usage == TB_TEX_USAGE_BRDF) {
    format = VK_FORMAT_R32G32_SFLOAT;
  } else if (usage == TB_TEX_USAGE_UNKNOWN) {
    TB_CHECK(false, "Unexpected texture usage");
  }

  // Allocate gpu image
  {
    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .arrayLayers = layers,
        .extent =
            (VkExtent3D){
                .width = width,
                .height = height,
                .depth = depth,
            },
        .format = format,
        .imageType = VK_IMAGE_TYPE_2D,
        .mipLevels = mip_levels,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    tb_rnd_sys_create_gpu_image(rnd_sys, pixels, size, &create_info, name,
                                &texture.gpu_image, &texture.host_buffer);
  }

  // Create image view
  {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.gpu_image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components =
            {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange =
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                mip_levels,
                0,
                layers,
            },
    };
    char view_name[100] = {0};
    SDL_snprintf(view_name, 100, "%s Image TbView", name); // NOLINT
    tb_rnd_create_image_view(rnd_sys, &create_info, view_name,
                             &texture.image_view);
  }

  // Issue uploads
  {
    TbBufferImageCopy *uploads =
        tb_alloc_nm_tp(rnd_sys->tmp_alloc, mip_levels, TbBufferImageCopy);

    TB_CHECK(mip_levels == 1, "Only expecting one mip level");
    uploads[0] = (TbBufferImageCopy){
        .src = texture.host_buffer.buffer,
        .dst = texture.gpu_image.image,
        .region =
            {
                .bufferOffset = texture.host_buffer.offset,
                .imageSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .layerCount = 1,
                    },
                .imageExtent =
                    {
                        .width = width,
                        .height = height,
                        .depth = 1,
                    },
            },
        .range =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = 0,
                .baseMipLevel = 0,
                .layerCount = 1,
                .levelCount = 1,
            },
    };

    // Will handle transitioning the image's layout to shader read only
    tb_rnd_upload_buffer_to_image(rnd_sys, uploads, mip_levels);
  }

  TracyCZoneEnd(ctx);
  return texture;
}

typedef struct TbTextureLoadedArgs {
  ecs_world_t *ecs;
  TbTexture2 tex;
  TbTextureImage comp;
} TbTextureLoadedArgs;

void tb_texture_loaded2(const void *args) {
  tb_auto loaded_args = (const TbTextureLoadedArgs *)args;
  tb_auto ecs = loaded_args->ecs;
  tb_auto tex = loaded_args->tex;
  if (tex != 0) {
    ecs_add(ecs, tex, TbTextureLoaded);
    ecs_set_ptr(ecs, tex, TbTextureImage, &loaded_args->comp);
  } else {
    TB_CHECK(false, "Texture load failed. Do we need to retry?");
  }
}

typedef struct TbLoadCommonTexture2Args {
  ecs_world_t *ecs;
  TbTexture2 tex;
  TbTaskScheduler enki;
  TbRenderSystem *rnd_sys;
  TbPinnedTask loaded_task;
  TbTextureUsage usage;
} TbLoadCommonTexture2Args;

typedef struct TbLoadGLTFTexture2Args {
  TbLoadCommonTexture2Args common;
  TbTextureGLTFLoadRequest gltf;
} TbLoadGLTFTexture2Args;

void tb_load_gltf_texture_task(const void *args) {
  TracyCZoneN(ctx, "Load GLTF Texture Task", true);
  tb_auto load_args = (const TbLoadGLTFTexture2Args *)args;
  TbTexture2 tex = load_args->common.tex;
  tb_auto rnd_sys = load_args->common.rnd_sys;
  tb_auto usage = load_args->common.usage;

  tb_auto path = load_args->gltf.path;
  tb_auto mat_name = load_args->gltf.mat_name;

  // tb_global_alloc is the only safe allocator to use in a task
  tb_auto data = tb_read_glb(tb_global_alloc, path);
  // Find material by name
  struct cgltf_material *mat = NULL;
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    tb_auto material = &data->materials[i];
    if (SDL_strcmp(mat_name, material->name) == 0) {
      mat = material;
      break;
    }
  }
  if (mat == NULL) {
    TB_CHECK(false, "Failed to find material by name");
    tex = 0; // Invalid ent means task failed
  }

  char image_name[100] = {0};
  struct cgltf_texture *texture = NULL;
  // Find image by usage
  switch (usage) {
  case TB_TEX_USAGE_COLOR: {
    if (mat->has_pbr_metallic_roughness) {
      texture = mat->pbr_metallic_roughness.base_color_texture.texture;
    } else if (mat->has_pbr_specular_glossiness) {
      texture = mat->pbr_specular_glossiness.diffuse_texture.texture;
    } else {
      TB_CHECK(false, "Expected material to have a color texture somewhere");
    }
    SDL_snprintf(image_name, 100, "%s_color", mat->name);
    break;
  }
  case TB_TEX_USAGE_METAL_ROUGH:
    if (mat->has_pbr_metallic_roughness) {
      texture = mat->pbr_metallic_roughness.metallic_roughness_texture.texture;
    } else {
      TB_CHECK(false, "Expected material to have metallic roughness model");
    }
    SDL_snprintf(image_name, 100, "%s_metal", mat->name);
    break;
  case TB_TEX_USAGE_NORMAL:
    texture = mat->normal_texture.texture;
    SDL_snprintf(image_name, 100, "%s_normal", mat->name);
    break;
  default:
    texture = NULL;
    break;
  }
  if (texture == NULL) {
    TB_CHECK(false, "Failed to find texture by usage");
    tex = 0; // Invalid ent means task failed
  }

  TbTextureImage tex_comp = {0};
  if (tex != 0) {
    tex_comp = tb_load_gltf_texture(rnd_sys, image_name, texture);
  }

  // Launch pinned task to handle loading signals on main thread
  TbTextureLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .tex = tex,
      .comp = tex_comp,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbTextureLoadedArgs));
  TracyCZoneEnd(ctx);
}

typedef struct TbLoadKTXTexture2Args {
  TbLoadCommonTexture2Args common;
  TbTextureKTXLoadRequest ktx;
} TbLoadKTXTexture2Args;

void tb_load_ktx_texture_task(const void *args) {
  TracyCZoneN(ctx, "Load KTX Texture Task", true);
  tb_auto load_args = (const TbLoadKTXTexture2Args *)args;
  TbTexture2 tex = load_args->common.tex;
  tb_auto rnd_sys = load_args->common.rnd_sys;

  tb_auto path = load_args->ktx.path;
  tb_auto name = load_args->ktx.name;

  ktxTexture2 *ktx = NULL;

  ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

  // We need to open this file with SDL_RWops because on a platform like
  // android where the asset lives in package storage, this is the best way
  // to actually open the file you're looking for
  SDL_RWops *tex_file = SDL_RWFromFile(path, "rb");
  size_t tex_size = SDL_RWsize(tex_file);

  uint8_t *tex_data = tb_alloc(tb_global_alloc, tex_size);
  SDL_RWread(tex_file, (void *)tex_data, tex_size);
  SDL_RWclose(tex_file);

  ktx_error_code_e err =
      ktxTexture2_CreateFromMemory(tex_data, tex_size, flags, &ktx);
  TB_CHECK(err == KTX_SUCCESS, "Failed to create KTX texture from memory");

  TbTextureImage tex_comp = {0};
  if (ktx != NULL) {
    tex_comp = tb_load_ktx_image(rnd_sys, name, ktx);
  }

  tb_free(tb_global_alloc, tex_data);

  // Launch pinned task to handle loading signals on main thread
  TbTextureLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .tex = tex,
      .comp = tex_comp,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbTextureLoadedArgs));
  TracyCZoneEnd(ctx);
}

typedef struct TbLoadRawTexture2Args {
  TbLoadCommonTexture2Args common;
  TbTextureRawLoadRequest raw;
} TbLoadRawTexture2Args;

void tb_load_raw_texture_task(const void *args) {
  TracyCZoneN(ctx, "Load Raw Texture Task", true);
  tb_auto load_args = (const TbLoadRawTexture2Args *)args;
  TbTexture2 tex = load_args->common.tex;
  tb_auto rnd_sys = load_args->common.rnd_sys;
  tb_auto usage = load_args->common.usage;

  tb_auto name = load_args->raw.name;
  tb_auto pixels = load_args->raw.pixels;
  tb_auto size = load_args->raw.size;
  tb_auto width = load_args->raw.width;
  tb_auto height = load_args->raw.height;

  TbTextureImage tex_comp =
      tb_load_raw_image(rnd_sys, name, pixels, size, width, height, usage);

  // Launch pinned task to handle loading signals on main thread
  TbTextureLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .tex = tex,
      .comp = tex_comp,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbTextureLoadedArgs));
  TracyCZoneEnd(ctx);
}

// Systems

void tb_queue_gltf_tex_loads(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Queue GLTF Tex Loads", true);
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto reqs = ecs_field(it, TbTextureGLTFLoadRequest, 3);
  tb_auto usages = ecs_field(it, TbTextureUsage, 4);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    TbTexture2 ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded2, NULL, 0);

    TbLoadGLTFTexture2Args args = {
        .common =
            {
                .ecs = it->world,
                .tex = ent,
                .enki = enki,
                .rnd_sys = rnd_sys,
                .loaded_task = loaded_task,
                .usage = usage,
            },
        .gltf = req,
    };
    TbTask load_task = tb_async_task(enki, tb_load_gltf_texture_task, &args,
                                     sizeof(TbLoadGLTFTexture2Args));
    // Apply task component to texture entity
    ecs_set(it->world, ent, TbTask, {load_task});

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(it->world, ent, TbTextureGLTFLoadRequest);
  }
  TracyCZoneEnd(ctx);
}

void tb_queue_ktx_tex_loads(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Queue KTX Tex Loads", true);
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto reqs = ecs_field(it, TbTextureKTXLoadRequest, 3);
  tb_auto usages = ecs_field(it, TbTextureUsage, 4);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    TbTexture2 ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded2, NULL, 0);

    TbLoadKTXTexture2Args args = {
        .common =
            {
                .ecs = it->world,
                .tex = ent,
                .enki = enki,
                .rnd_sys = rnd_sys,
                .loaded_task = loaded_task,
                .usage = usage,
            },
        .ktx = req,
    };
    TbTask load_task = tb_async_task(enki, tb_load_ktx_texture_task, &args,
                                     sizeof(TbLoadKTXTexture2Args));
    // Apply task component to texture entity
    ecs_set(it->world, ent, TbTask, {load_task});

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(it->world, ent, TbTextureKTXLoadRequest);
  }
  TracyCZoneEnd(ctx);
}

void tb_queue_raw_tex_loads(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Queue Raw Tex Loads", true);
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto reqs = ecs_field(it, TbTextureRawLoadRequest, 3);
  tb_auto usages = ecs_field(it, TbTextureUsage, 4);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    TbTexture2 ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded2, NULL, 0);

    TbLoadRawTexture2Args args = {
        .common =
            {
                .ecs = it->world,
                .tex = ent,
                .enki = enki,
                .rnd_sys = rnd_sys,
                .loaded_task = loaded_task,
                .usage = usage,
            },
        .raw = req,
    };
    TbTask load_task = tb_async_task(enki, tb_load_raw_texture_task, &args,
                                     sizeof(TbLoadRawTexture2Args));
    // Apply task component to texture entity
    ecs_set(it->world, ent, TbTask, {load_task});

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(it->world, ent, TbTextureRawLoadRequest);
  }
  TracyCZoneEnd(ctx);
}

void tb_update_texture_descriptors(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Update Texture Descriptors", true);

  tb_auto tex_ctx = ecs_field(it, TbTexture2Ctx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto world = ecs_field(it, TbWorldRef, 3)->world;
  tb_auto tex_comps = ecs_field(it, TbTextureImage, 4);

  tb_auto tex_count = (uint64_t)it->count;

  if (tex_count != tex_ctx->set_pool.capacity) {
    tex_ctx->set_pool.capacity = tex_count;
    const uint64_t desc_count = tex_ctx->set_pool.capacity;

    // Re-create pool and allocate the one set that everything will be bound to
    {
      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
          .maxSets = tex_ctx->set_pool.capacity,
          .poolSizeCount = 1,
          .pPoolSizes =
              (VkDescriptorPoolSize[1]){
                  {
                      .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                      .descriptorCount = desc_count * 4,
                  },
              },
      };
      VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
          .sType =
              VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
          .descriptorSetCount = 1,
          .pDescriptorCounts = (uint32_t[1]){tex_count},
      };
      tb_rnd_resize_desc_pool(rnd_sys, &create_info, &tex_ctx->set_layout,
                              &alloc_info, &tex_ctx->set_pool, 1);
    }
  }

  // Write all textures into the descriptor set table
  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  {
    TB_DYN_ARR_RESET(writes, world->tmp_alloc, tex_count);
    tb_auto image_info =
        tb_alloc_nm_tp(world->tmp_alloc, tex_count, VkDescriptorImageInfo);

    for (uint64_t i = 0; i < tex_count; ++i) {
      tb_auto texture = &tex_comps[i];

      image_info[i] = (VkDescriptorImageInfo){
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView = texture->image_view,
      };

      tb_auto write = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .dstSet = tex_ctx->set_pool.sets[0],
          .dstArrayElement = i,
          .pImageInfo = &image_info[i],
      };
      TB_DYN_ARR_APPEND(writes, write);

      // Texture is now ready to be referenced elsewhere
      ecs_set(it->world, it->entities[i], TbTextureComponent2, {i});
    }
  }
  tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);

  TracyCZoneEnd(ctx);
}

// Toybox Glue

void tb_register_texture2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbTexture2Ctx);
  ECS_COMPONENT_DEFINE(ecs, TbTextureGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTextureKTXLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTextureRawLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTextureImage);
  ECS_COMPONENT_DEFINE(ecs, TbTextureComponent2);
  ECS_COMPONENT_DEFINE(ecs, TbTextureUsage);
  ECS_TAG_DEFINE(ecs, TbTextureLoaded);
  ECS_TAG_DEFINE(ecs, TbNeedTexDescUpdate);

  ECS_SYSTEM(ecs, tb_queue_gltf_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
             [in] TbTextureGLTFLoadRequest, [in] TbTextureUsage);
  ECS_SYSTEM(ecs, tb_queue_ktx_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
             [in] TbTextureKTXLoadRequest, [in] TbTextureUsage);
  ECS_SYSTEM(ecs, tb_queue_raw_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
             [in] TbTextureRawLoadRequest, [in] TbTextureUsage);

  ECS_SYSTEM(ecs, tb_update_texture_descriptors,
             EcsPreStore, [in] TbTexture2Ctx(TbTexture2Ctx),
             [in] TbRenderSystem(TbRenderSystem), [in] TbWorldRef(TbWorldRef),
             [in] TbTextureImage, TbTextureLoaded);

  TbTexture2Ctx ctx = {0};

  // Create descriptor set layout
  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    const uint32_t binding_count = 1;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = binding_count,
                .pBindingFlags =
                    (VkDescriptorBindingFlags[binding_count]){flags},
            },
        .bindingCount = binding_count,
        .pBindings =
            (VkDescriptorSetLayoutBinding[binding_count]){
                {
                    .binding = 0,
                    .descriptorCount = 2048, // HACK: High upper bound
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
    };
    tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Texture Table Layout",
                             &ctx.set_layout);
  }

  // Must set ctx before we try to load any textures
  ecs_singleton_set_ptr(ecs, TbTexture2Ctx, &ctx);

  // Load some default textures
  {
    const uint8_t pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    ctx.default_color_tex =
        tb_tex_sys_load_raw_tex(ecs, "Default Color Texture", pixels,
                                sizeof(pixels), 2, 2, TB_TEX_USAGE_COLOR);
  }
  {
    const uint8_t pixels[] = {
        0x7E, 0x7E, 0xFF, 255, 0x7E, 0x7E, 0xFF, 255,
        0x7E, 0x7E, 0xFF, 255, 0x7E, 0x7E, 0xFF, 255,
    };
    ctx.default_normal_tex =
        tb_tex_sys_load_raw_tex(ecs, "Default Normal Texture", pixels,
                                sizeof(pixels), 2, 2, TB_TEX_USAGE_NORMAL);
  }
  {
    const uint8_t pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    ctx.default_metal_rough_tex =
        tb_tex_sys_load_raw_tex(ecs, "Default Metal Rough Texture", pixels,
                                sizeof(pixels), 2, 2, TB_TEX_USAGE_METAL_ROUGH);
  }
  // Load BRDF LUT texture
  {
    const char *path = ASSET_PREFIX "textures/brdf.ktx2";
    ctx.brdf_tex =
        tb_tex_sys_load_ktx_tex(ecs, path, "BRDF LUT", TB_TEX_USAGE_BRDF);
  }

  ecs_singleton_set_ptr(ecs, TbTexture2Ctx, &ctx);
}

void tb_unregister_texture2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbTexture2Ctx);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);

  // TODO: Release all default texture references

  // TODO: Check for leaks

  // TODO: Clean up descriptor pool

  ecs_singleton_remove(ecs, TbTexture2Ctx);
}

TB_REGISTER_SYS(tb, texture2, TB_TEX_SYS_PRIO)

// Public API

VkDescriptorSet tb_tex_sys_get_set2(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbTexture2Ctx);
  return ctx->set_pool.sets[0];
}

TbTexture2 tb_tex_sys_load_raw_tex(ecs_world_t *ecs, const char *name,
                                   const uint8_t *pixels, uint64_t size,
                                   uint32_t width, uint32_t height,
                                   TbTextureUsage usage) {
  // Create a texture entity
  TbTexture2 tex_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, tex_ent, name);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, tex_ent, EcsChildOf, ecs_id(TbTexture2Ctx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, tex_ent, TbTextureRawLoadRequest,
          {name, pixels, size, width, height});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});

  return tex_ent;
}

TbTexture2 tb_tex_sys_load_mat_tex(ecs_world_t *ecs, const char *path,
                                   const char *mat_name, TbTextureUsage usage) {
  // Create a texture entity
  TbTexture2 tex_ent = ecs_new_entity(ecs, 0);

  // GLTFpack strips image names so we have to synthesize something
  {
    const uint32_t image_name_max = 100;
    char image_name[image_name_max] = {0};

    switch (usage) {
    case TB_TEX_USAGE_BRDF:
    default:
      TB_CHECK(false,
               "Material textures should have Color, Metal or Normal usage");
      break;
    case TB_TEX_USAGE_COLOR:
      SDL_snprintf(image_name, image_name_max, "%s_color", mat_name);
      break;
    case TB_TEX_USAGE_METAL_ROUGH:
      SDL_snprintf(image_name, image_name_max, "%s_metal", mat_name);
      break;
    case TB_TEX_USAGE_NORMAL:
      SDL_snprintf(image_name, image_name_max, "%s_normal", mat_name);
      break;
    }

    ecs_set_name(ecs, tex_ent, image_name);
  }

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, tex_ent, EcsChildOf, ecs_id(TbTexture2Ctx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, tex_ent, TbTextureGLTFLoadRequest, {path, mat_name});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});

  return tex_ent;
}

TbTexture2 tb_tex_sys_load_ktx_tex(ecs_world_t *ecs, const char *path,
                                   const char *name, TbTextureUsage usage) {
  // Create a texture entity
  TbTexture2 tex_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, tex_ent, name);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, tex_ent, EcsChildOf, ecs_id(TbTexture2Ctx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, tex_ent, TbTextureKTXLoadRequest, {path, name});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});

  return tex_ent;
}

bool tb_is_texture_ready(ecs_world_t *ecs, TbTexture2 tex) {
  return ecs_has(ecs, tex, TbTextureLoaded) &&
         ecs_has(ecs, tex, TbTextureComponent2);
}
