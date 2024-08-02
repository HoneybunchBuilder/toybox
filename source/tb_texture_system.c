#include "tb_texture_system.h"

#include "tb_assets.h"
#include "tb_common.h"
#include "tb_descriptor_buffer.h"
#include "tb_gltf.h"
#include "tb_ktx.h"
#include "tb_queue.h"
#include "tb_task_scheduler.h"
#include "tb_world.h"

static const int32_t TbMaxParallelTextureLoads = 8;

ECS_TAG_DECLARE(TbTexLoadPhaseLoading);
ECS_TAG_DECLARE(TbTexLoadPhaseLoaded);
ECS_TAG_DECLARE(TbTexLoadPhaseWriting);
ECS_TAG_DECLARE(TbTexLoadPhaseWritten);
ECS_TAG_DECLARE(TbTexLoadPhaseReady);

typedef SDL_AtomicInt TbTexQueueCounter;
ECS_COMPONENT_DECLARE(TbTexQueueCounter);

ECS_COMPONENT_DECLARE(TbTextureUsage);

typedef struct TbTextureCtx {
  VkDescriptorSetLayout set_layout;
  TbFrameDescriptorPoolList frame_set_pool;
  uint32_t owned_tex_count;
  uint32_t pool_update_counter;

  VkDescriptorSetLayout set_layout2;
  TbDescriptorBuffer desc_buffer;

  // Custom queries for special systems
  ecs_query_t *tex_query;
  ecs_query_t *loaded_tex_query;
  ecs_query_t *dirty_tex_query;
  ecs_query_t *ready_tex_query;

  TbTexture default_color_tex;
  TbTexture default_normal_tex;
  TbTexture default_metal_rough_tex;
  TbTexture brdf_tex;
} TbTextureCtx;
ECS_COMPONENT_DECLARE(TbTextureCtx);

typedef struct TbTextureImage {
  uint32_t ref_count;
  TbHostBuffer host_buffer;
  TbImage gpu_image;
  VkImageView image_view;
} TbTextureImage;
ECS_COMPONENT_DECLARE(TbTextureImage);

ECS_COMPONENT_DECLARE(TbTextureComponent);

// Describes the creation of a texture that lives in a GLB file
typedef struct TbTextureGLTFLoadRequest {
  const cgltf_data *data;
  const char *mat_name;
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

// Internals

typedef struct KTX2IterData {
  VkBuffer buffer;
  VkImage image;
  TbBufferImageCopy *uploads;
  uint64_t offset;
} KTX2IterData;

ktx_error_code_e iterate_ktx2_levels(int32_t mip_level, int32_t face,
                                     int32_t width, int32_t height,
                                     int32_t depth, uint64_t face_lod_size,
                                     void *pixels, void *userdata) {
  (void)pixels;
  KTX2IterData *user_data = (KTX2IterData *)userdata;

  user_data->uploads[mip_level] = (TbBufferImageCopy){
      .src = user_data->buffer,
      .dst = user_data->image,
      .region =
          {
              .bufferOffset = user_data->offset,
              .imageSubresource =
                  {
                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                      .layerCount = 1,
                      .baseArrayLayer = face,
                      .mipLevel = mip_level,
                  },
              .imageExtent =
                  {
                      .width = width,
                      .height = height,
                      .depth = depth,
                  },
          },
      .range =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseArrayLayer = face,
              .baseMipLevel = mip_level,
              .layerCount = 1,
              .levelCount = 1,
          },
  };

  user_data->offset += face_lod_size;
  return KTX_SUCCESS;
}

VkImageType get_ktx2_image_type(const ktxTexture2 *t) {
  return (VkImageType)(t->numDimensions - 1);
}

VkImageViewType get_ktx2_image_view_type(const ktxTexture2 *t) {
  const VkImageType img_type = get_ktx2_image_type(t);

  const bool cube = t->isCubemap;
  const bool array = t->isArray;

  if (img_type == VK_IMAGE_TYPE_1D) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    } else {
      return VK_IMAGE_VIEW_TYPE_1D;
    }
  } else if (img_type == VK_IMAGE_TYPE_2D) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
      return VK_IMAGE_VIEW_TYPE_2D;
    }

  } else if (img_type == VK_IMAGE_TYPE_3D) {
    // No such thing as a 3D array
    return VK_IMAGE_VIEW_TYPE_3D;
  } else if (cube) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_CUBE;
  }

  TB_CHECK(false, "Invalid");
  return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

TbTextureImage tb_load_ktx_image(TbRenderSystem *rnd_sys, const char *name,
                                 ktxTexture2 *ktx) {
  TB_TRACY_SCOPE("Load KTX Texture");
  bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
  if (needs_transcoding) {
    TB_TRACY_SCOPE("KTX Basis Transcode");
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
  TB_TRACY_SCOPE("Load GLTF Texture");
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

  return tex;
}

TbTextureImage tb_load_raw_image(TbRenderSystem *rnd_sys, const char *name,
                                 const uint8_t *pixels, uint64_t size,
                                 uint32_t width, uint32_t height,
                                 TbTextureUsage usage) {
  TB_TRACY_SCOPE("Load Raw Texture");

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

  return texture;
}

typedef struct TbTextureLoadedArgs {
  ecs_world_t *ecs;
  TbTexture tex;
  TbTextureImage comp;
} TbTextureLoadedArgs;

void tb_texture_loaded(const void *args) {
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

typedef struct TbLoadCommonTextureArgs {
  ecs_world_t *ecs;
  TbTexture tex;
  TbTaskScheduler enki;
  TbRenderSystem *rnd_sys;
  TbPinnedTask loaded_task;
  TbTextureUsage usage;
} TbLoadCommonTextureArgs;

typedef struct TbLoadGLTFTexture2Args {
  TbLoadCommonTextureArgs common;
  TbTextureGLTFLoadRequest gltf;
} TbLoadGLTFTexture2Args;

void tb_load_gltf_texture_task(const void *args) {
  TB_TRACY_SCOPE("Load GLTF Texture Task");
  tb_auto load_args = (const TbLoadGLTFTexture2Args *)args;
  TbTexture tex = load_args->common.tex;
  tb_auto rnd_sys = load_args->common.rnd_sys;
  tb_auto usage = load_args->common.usage;

  tb_auto data = load_args->gltf.data;
  tb_auto mat_name = load_args->gltf.mat_name;

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

  // Strings were copies that can be freed now
  tb_free(tb_global_alloc, (void *)mat_name);

  // Launch pinned task to handle loading signals on main thread
  TbTextureLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .tex = tex,
      .comp = tex_comp,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbTextureLoadedArgs));
}

typedef struct TbLoadKTXTexture2Args {
  TbLoadCommonTextureArgs common;
  TbTextureKTXLoadRequest ktx;
} TbLoadKTXTexture2Args;

void tb_load_ktx_texture_task(const void *args) {
  TB_TRACY_SCOPE("Load KTX Texture Task");
  tb_auto load_args = (const TbLoadKTXTexture2Args *)args;
  TbTexture tex = load_args->common.tex;
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

  uint8_t *tex_data = tb_alloc(tb_thread_alloc, tex_size);
  SDL_RWread(tex_file, (void *)tex_data, tex_size);
  SDL_RWclose(tex_file);

  ktx_error_code_e err =
      ktxTexture2_CreateFromMemory(tex_data, tex_size, flags, &ktx);
  TB_CHECK(err == KTX_SUCCESS, "Failed to create KTX texture from memory");

  TbTextureImage tex_comp = {0};
  if (ktx != NULL) {
    tex_comp = tb_load_ktx_image(rnd_sys, name, ktx);
  }

  tb_free(tb_thread_alloc, tex_data);

  // Launch pinned task to handle loading signals on main thread
  TbTextureLoadedArgs loaded_args = {
      .ecs = load_args->common.ecs,
      .tex = tex,
      .comp = tex_comp,
  };
  tb_launch_pinned_task_args(load_args->common.enki,
                             load_args->common.loaded_task, &loaded_args,
                             sizeof(TbTextureLoadedArgs));
}

typedef struct TbLoadRawTextureArgs {
  TbLoadCommonTextureArgs common;
  TbTextureRawLoadRequest raw;
} TbLoadRawTextureArgs;

void tb_load_raw_texture_task(const void *args) {
  TB_TRACY_SCOPE("Load Raw Texture Task");
  tb_auto load_args = (const TbLoadRawTextureArgs *)args;
  TbTexture tex = load_args->common.tex;
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
}

// Systems

void tb_queue_gltf_tex_loads(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Queue GLTF Tex Loads");

  tb_auto ecs = it->world;

  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto counter = ecs_field(it, TbTexQueueCounter, 3);
  tb_auto reqs = ecs_field(it, TbTextureGLTFLoadRequest, 4);
  tb_auto usages = ecs_field(it, TbTextureUsage, 5);

  tb_auto tex_ctx = ecs_singleton_get_mut(it->world, TbTextureCtx);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelTextureLoads) {
      break;
    }
    TbTexture ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded, NULL, 0);

    TbLoadGLTFTexture2Args args = {
        .common =
            {
                .ecs = ecs,
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
    ecs_set(ecs, ent, TbTask, {load_task});

    SDL_AtomicIncRef(counter);
    tex_ctx->owned_tex_count++;
    tb_tex_sys_begin_load(ecs);

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(ecs, ent, TbTextureGLTFLoadRequest);
  }
}

void tb_queue_ktx_tex_loads(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Queue KTX Tex Loads");

  tb_auto ecs = it->world;

  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto counter = ecs_field(it, TbTexQueueCounter, 3);
  tb_auto reqs = ecs_field(it, TbTextureKTXLoadRequest, 4);
  tb_auto usages = ecs_field(it, TbTextureUsage, 5);

  tb_auto tex_ctx = ecs_singleton_get_mut(it->world, TbTextureCtx);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelTextureLoads) {
      break;
    }
    TbTexture ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded, NULL, 0);

    TbLoadKTXTexture2Args args = {
        .common =
            {
                .ecs = ecs,
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
    ecs_set(ecs, ent, TbTask, {load_task});

    SDL_AtomicIncRef(counter);
    tex_ctx->owned_tex_count++;
    tb_tex_sys_begin_load(ecs);

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(ecs, ent, TbTextureKTXLoadRequest);
  }
}

void tb_queue_raw_tex_loads(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Queue Raw Tex Loads");

  tb_auto ecs = it->world;

  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto counter = ecs_field(it, TbTexQueueCounter, 3);
  tb_auto reqs = ecs_field(it, TbTextureRawLoadRequest, 4);
  tb_auto usages = ecs_field(it, TbTextureUsage, 5);

  tb_auto tex_ctx = ecs_singleton_get_mut(it->world, TbTextureCtx);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    if (SDL_AtomicGet(counter) > TbMaxParallelTextureLoads) {
      break;
    }

    TbTexture ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded, NULL, 0);

    TbLoadRawTextureArgs args = {
        .common =
            {
                .ecs = ecs,
                .tex = ent,
                .enki = enki,
                .rnd_sys = rnd_sys,
                .loaded_task = loaded_task,
                .usage = usage,
            },
        .raw = req,
    };
    TbTask load_task = tb_async_task(enki, tb_load_raw_texture_task, &args,
                                     sizeof(TbLoadRawTextureArgs));
    // Apply task component to texture entity
    ecs_set(ecs, ent, TbTask, {load_task});

    SDL_AtomicIncRef(counter);
    tex_ctx->owned_tex_count++;
    tb_tex_sys_begin_load(ecs);

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(ecs, ent, TbTextureRawLoadRequest);
  }
}

void tb_reset_tex_queue_count(ecs_iter_t *it) {
  tb_auto counter = ecs_field(it, TbTexQueueCounter, 1);
  SDL_AtomicSet(counter, 0);
}

void tb_tex_phase_loading(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Texture Loading Phase");

  tb_auto tex_ctx = ecs_field(it, TbTextureCtx, 1);

  if (tex_ctx->owned_tex_count == 0) {
    return;
  }

  uint64_t loaded_tex_count = 0;
  ecs_iter_t tex_it = ecs_query_iter(it->world, tex_ctx->loaded_tex_query);
  while (ecs_query_next(&tex_it)) {
    loaded_tex_count += tex_it.count;
  }

  // We are in the loading phase and all textures have loaded
  // Move on to the next phase
  if (tex_ctx->owned_tex_count == loaded_tex_count) {
    ecs_remove(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseLoading);
    ecs_add(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseLoaded);
    tex_ctx->pool_update_counter = 0;
  }
}

void tb_update_texture_pool(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Update Texture Pool");
#if TB_USE_DESC_BUFFER == 1
  // Skip this phase
  ecs_remove(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseLoaded);
  ecs_add(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWriting);
#else
  tb_auto tex_ctx = ecs_field(it, TbTextureCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);

  uint64_t tex_count = tex_ctx->owned_tex_count;

  // Resize the pool if necessary
  if (tex_count != tb_rnd_frame_desc_pool_get_desc_count(
                       rnd_sys, tex_ctx->frame_set_pool.pools)) {
    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount = 4,
                },
            },
    };
    VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = (uint32_t[1]){tex_count},
    };
    tb_rnd_frame_desc_pool_tick(rnd_sys, "texture", &create_info,
                                &tex_ctx->set_layout, &alloc_info,
                                tex_ctx->frame_set_pool.pools, 1, tex_count);
  }

  // When we get to this phase all textures must be marked dirty
  tb_auto tex_it = ecs_query_iter(it->world, tex_ctx->loaded_tex_query);
  while (ecs_query_next(&tex_it)) {
    tb_auto ecs = tex_it.world;
    for (int32_t i = 0; i < tex_it.count; ++i) {
      tb_auto tex_ent = tex_it.entities[i];
      ecs_add(ecs, tex_ent, TbNeedsDescriptorUpdate);
      ecs_remove(ecs, tex_ent, TbDescriptorReady);
      if (!ecs_has(ecs, tex_ent, TbDescriptorCounter)) {
        ecs_set(ecs, tex_ent, TbDescriptorCounter, {0});
      } else {
        tb_auto counter = ecs_get_mut(ecs, tex_ent, TbDescriptorCounter);
        SDL_AtomicSet(counter, 0);
      }
    }
  }

  tex_ctx->pool_update_counter++;

  // One pool must be resized per frame
  if (tex_ctx->pool_update_counter >= TB_MAX_FRAME_STATES) {
    ecs_remove(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseLoaded);
    ecs_add(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWriting);

    // Reset counter for next phase
    tex_ctx->pool_update_counter = 0;
  }
#endif
}

void tb_write_texture_descriptors(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Write Texture Descriptors");

  tb_auto tex_ctx = ecs_field(it, TbTextureCtx, 1);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 2);
  tb_auto world = ecs_singleton_get(it->world, TbWorldRef)->world;

  // We care about writing descriptors for all owned textures
  uint64_t tex_count = tex_ctx->owned_tex_count;
  if (tex_count == 0) {
    return;
  }

  // Write all dirty textures into the descriptor set table
  uint32_t tex_idx = 0;
  tb_auto tex_it = ecs_query_iter(it->world, tex_ctx->dirty_tex_query);

// Alternate path: write to descriptor buffer
#if TB_USE_DESC_BUFFER == 1
  while (ecs_query_next(&tex_it)) {
    tb_auto textures = ecs_field(&tex_it, TbTextureImage, 1);
    for (int32_t i = 0; i < tex_it.count; ++i) {
      tb_auto texture = &textures[i];
      TbDescriptor desc = {
          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .data = {.pSampledImage = &(VkDescriptorImageInfo){
                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       .imageView = texture->image_view,
                   }}};
      uint32_t idx =
          tb_write_desc_to_buffer(rnd_sys, &tex_ctx->desc_buffer, 0, &desc);

      ecs_set(it->world, tex_it.entities[i], TbTextureComponent, {idx});
      ecs_add(it->world, tex_it.entities[i], TbDescriptorReady);
    }
  }
  ecs_remove(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWriting);
  ecs_add(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWritten);
#else
  // We can't write more than this number of textures
  const uint64_t desc_count = tb_rnd_frame_desc_pool_get_desc_count(
      rnd_sys, tex_ctx->frame_set_pool.pools);

  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  TB_DYN_ARR_OF(VkDescriptorImageInfo) img_info = {0};
  TB_DYN_ARR_RESET(writes, world->tmp_alloc, desc_count);
  TB_DYN_ARR_RESET(img_info, world->tmp_alloc, desc_count);
  while (ecs_query_next(&tex_it)) {
    tb_auto textures = ecs_field(&tex_it, TbTextureImage, 1);
    if (tex_idx >= desc_count) {
      break;
    }
    for (int32_t i = 0; i < tex_it.count; ++i) {
      if (tex_idx >= desc_count) {
        break;
      }

      tb_auto texture = &textures[i];

      VkDescriptorImageInfo image_info = {
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView = texture->image_view,
      };
      TB_DYN_ARR_APPEND(img_info, image_info);

      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .dstSet = tb_rnd_frame_desc_pool_get_set(
              rnd_sys, tex_ctx->frame_set_pool.pools, 0),
          .dstArrayElement = tex_idx,
          .pImageInfo = &TB_DYN_ARR_AT(img_info, tex_idx),
      };
      TB_DYN_ARR_APPEND(writes, write);

      // Texture is now ready to be referenced elsewhere
      ecs_set(it->world, tex_it.entities[i], TbTextureComponent, {tex_idx});
      ecs_add(it->world, tex_it.entities[i], TbUpdatingDescriptor);
      tex_idx++;
    }
  }
  tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);

  tex_ctx->pool_update_counter++;

  // One write must be enqueued per frame
  if (tex_ctx->pool_update_counter >= TB_MAX_FRAME_STATES) {
    ecs_remove(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWriting);
    ecs_add(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWritten);
    tex_ctx->pool_update_counter = 0;
  }
#endif
}

void tb_tex_phase_written(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Texture Written Phase");

  tb_auto tex_ctx = ecs_field(it, TbTextureCtx, 1);

  uint32_t total_tex_count = tex_ctx->owned_tex_count;

  uint32_t ready_tex_count = 0;
  ecs_iter_t tex_it = ecs_query_iter(it->world, tex_ctx->ready_tex_query);
  while (ecs_query_next(&tex_it)) {
    ready_tex_count += tex_it.count;
  }

  // When all textures are ready we can consider this phase over
  if (total_tex_count > 0 && total_tex_count == ready_tex_count) {
    ecs_remove(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseWritten);
    ecs_add(it->world, ecs_id(TbTextureCtx), TbTexLoadPhaseReady);
  }
}

// Toybox Glue

void tb_register_texture_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbTextureCtx);
  ECS_COMPONENT_DEFINE(ecs, TbTextureGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTextureKTXLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTextureRawLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTextureImage);
  ECS_COMPONENT_DEFINE(ecs, TbTextureComponent);
  ECS_COMPONENT_DEFINE(ecs, TbTextureUsage);
  ECS_COMPONENT_DEFINE(ecs, TbTexQueueCounter);
  ECS_TAG_DEFINE(ecs, TbTextureLoaded);
  ECS_TAG_DEFINE(ecs, TbTexLoadPhaseLoading);
  ECS_TAG_DEFINE(ecs, TbTexLoadPhaseLoaded);
  ECS_TAG_DEFINE(ecs, TbTexLoadPhaseWriting);
  ECS_TAG_DEFINE(ecs, TbTexLoadPhaseWritten);
  ECS_TAG_DEFINE(ecs, TbTexLoadPhaseReady);

  ECS_SYSTEM(ecs, tb_queue_gltf_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
             TbTexQueueCounter(TbTexQueueCounter),
             [in] TbTextureGLTFLoadRequest, [in] TbTextureUsage);
  ECS_SYSTEM(ecs, tb_queue_ktx_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
             TbTexQueueCounter(TbTexQueueCounter), [in] TbTextureKTXLoadRequest,
             [in] TbTextureUsage);
  ECS_SYSTEM(ecs, tb_queue_raw_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
             TbTexQueueCounter(TbTexQueueCounter), [in] TbTextureRawLoadRequest,
             [in] TbTextureUsage);

  ECS_SYSTEM(ecs, tb_reset_tex_queue_count,
             EcsPostUpdate, [in] TbTexQueueCounter(TbTexQueueCounter));

  ECS_SYSTEM(ecs, tb_tex_phase_loading, EcsPreFrame, TbTextureCtx(TbTextureCtx),
             TbTexLoadPhaseLoading);
  ECS_SYSTEM(ecs, tb_update_texture_pool,
             EcsPostUpdate, [in] TbTextureCtx(TbTextureCtx),
             [in] TbRenderSystem(TbRenderSystem), TbTexLoadPhaseLoaded);
  ECS_SYSTEM(ecs, tb_write_texture_descriptors,
             EcsPreStore, [in] TbTextureCtx(TbTextureCtx),
             [in] TbRenderSystem(TbRenderSystem), TbTexLoadPhaseWriting);
  ECS_SYSTEM(ecs, tb_tex_phase_written, EcsOnStore, TbTextureCtx(TbTextureCtx),
             TbTexLoadPhaseWritten);

  TbTextureCtx ctx = {
      .tex_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbTextureImage), .inout = EcsIn},
                    }}),
      .loaded_tex_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbTextureImage), .inout = EcsIn},
                        {.id = ecs_id(TbTextureLoaded)},
                    }}),
      .dirty_tex_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbTextureImage), .inout = EcsIn},
                        {.id = ecs_id(TbTextureLoaded)},
                        {.id = ecs_id(TbNeedsDescriptorUpdate)},
                    }}),
      .ready_tex_query = ecs_query(
          ecs, {.filter.terms =
                    {
                        {.id = ecs_id(TbTextureImage), .inout = EcsIn},
                        {.id = ecs_id(TbTextureLoaded)},
                        {.id = ecs_id(TbTextureComponent)},
                        {.id = ecs_id(TbDescriptorReady)},
                    }}),
  };

  TbTexQueueCounter queue_count = {0};
  SDL_AtomicSet(&queue_count, 0);
  ecs_singleton_set_ptr(ecs, TbTexQueueCounter, &queue_count);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

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
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Texture Table Layout",
                             &ctx.set_layout);
  }

  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    const uint32_t binding_count = 1;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
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
    tb_rnd_create_set_layout(rnd_sys, &create_info,
                             "Texture Desc Buffer Layout", &ctx.set_layout2);
    tb_create_descriptor_buffer(rnd_sys, ctx.set_layout2, "Texture Descriptors",
                                4, &ctx.desc_buffer);
  }

  // Must set ctx before we try to load any textures
  ecs_singleton_set_ptr(ecs, TbTextureCtx, &ctx);

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

  ecs_singleton_set_ptr(ecs, TbTextureCtx, &ctx);
}

void tb_unregister_texture_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbTextureCtx);

  ecs_query_fini(ctx->tex_query);
  ecs_query_fini(ctx->loaded_tex_query);
  ecs_query_fini(ctx->dirty_tex_query);
  ecs_query_fini(ctx->ready_tex_query);

  tb_destroy_descriptor_buffer(rnd_sys, &ctx->desc_buffer);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);

  // TODO: Release all default texture references

  // TODO: Check for leaks

  // TODO: Clean up descriptor pool

  ecs_singleton_remove(ecs, TbTextureCtx);
}

TB_REGISTER_SYS(tb, texture, TB_TEX_SYS_PRIO)

// Public API

VkDescriptorSetLayout tb_tex_sys_get_set_layout(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbTextureCtx);
#if TB_USE_DESC_BUFFER == 1
  return ctx->set_layout2;
#else
  return ctx->set_layout;
#endif
}

VkDescriptorSet tb_tex_sys_get_set(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbTextureCtx);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  return tb_rnd_frame_desc_pool_get_set(rnd_sys, ctx->frame_set_pool.pools, 0);
}

// Returns the binding info of the texture system's descriptor buffer
VkDescriptorBufferBindingInfoEXT tb_tex_sys_get_table_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbTextureCtx);
  return tb_desc_buff_get_binding(&ctx->desc_buffer);
}

VkImageView tb_tex_sys_get_image_view2(ecs_world_t *ecs, TbTexture tex) {
  TB_CHECK_RETURN(tb_is_texture_ready(ecs, tex),
                  "Tried to get image view of a texture that wasn't ready",
                  VK_NULL_HANDLE);
  tb_auto image = ecs_get(ecs, tex, TbTextureImage);
  return image->image_view;
}

void tb_tex_sys_begin_load(ecs_world_t *ecs) {
  if (!ecs_has(ecs, ecs_id(TbTextureCtx), TbTexLoadPhaseLoading)) {
    ecs_remove(ecs, ecs_id(TbTextureCtx), TbTexLoadPhaseLoaded);
    ecs_remove(ecs, ecs_id(TbTextureCtx), TbTexLoadPhaseWriting);
    ecs_remove(ecs, ecs_id(TbTextureCtx), TbTexLoadPhaseWritten);
    ecs_remove(ecs, ecs_id(TbTextureCtx), TbTexLoadPhaseReady);
    ecs_add(ecs, ecs_id(TbTextureCtx), TbTexLoadPhaseLoading);
  }
}

TbTexture tb_tex_sys_load_raw_tex(ecs_world_t *ecs, const char *name,
                                  const uint8_t *pixels, uint64_t size,
                                  uint32_t width, uint32_t height,
                                  TbTextureUsage usage) {
  // If an entity already exists with this name it is either loading or loaded
  TbTexture tex_ent = ecs_lookup_child(ecs, ecs_id(TbTextureCtx), name);
  if (tex_ent != 0) {
    return tex_ent;
  }

  // Create a texture entity
  tex_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, tex_ent, name);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, tex_ent, EcsChildOf, ecs_id(TbTextureCtx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, tex_ent, TbTextureRawLoadRequest,
          {name, pixels, size, width, height});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});
  ecs_add(ecs, tex_ent, TbNeedsDescriptorUpdate);

  return tex_ent;
}

TbTexture tb_tex_sys_load_mat_tex(ecs_world_t *ecs, const cgltf_data *data,
                                  const char *mat_name, TbTextureUsage usage) {
  const uint32_t image_name_max = 100;
  char image_name[image_name_max] = {0};
  // GLTFpack strips image names so we have to synthesize something
  {
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
  }

  // If an entity already exists with this name it is either loading or loaded
  TbTexture tex_ent = ecs_lookup_child(ecs, ecs_id(TbTextureCtx), image_name);
  if (tex_ent != 0) {
    return tex_ent;
  }

  TB_CHECK_RETURN(data, "Expected Data", 0);

  // Create a texture entity
  tex_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, tex_ent, image_name);

  // Need to copy strings for task safety
  // Task is responsible for freeing this name
  const size_t mat_name_len = SDL_strnlen(mat_name, 256) + 1;
  char *mat_name_cpy = tb_alloc_nm_tp(tb_global_alloc, mat_name_len, char);
  SDL_strlcpy(mat_name_cpy, mat_name, mat_name_len);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, tex_ent, EcsChildOf, ecs_id(TbTextureCtx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, tex_ent, TbTextureGLTFLoadRequest, {data, mat_name_cpy});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});
  ecs_add(ecs, tex_ent, TbNeedsDescriptorUpdate);

  return tex_ent;
}

TbTexture tb_tex_sys_load_ktx_tex(ecs_world_t *ecs, const char *path,
                                  const char *name, TbTextureUsage usage) {
  // If an entity already exists with this name it is either loading or loaded
  TbTexture tex_ent = ecs_lookup(ecs, name);
  if (tex_ent != 0) {
    return tex_ent;
  }

  // Create a texture entity
  tex_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, tex_ent, name);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, tex_ent, EcsChildOf, ecs_id(TbTextureCtx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, tex_ent, TbTextureKTXLoadRequest, {path, name});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});
  ecs_add(ecs, tex_ent, TbNeedsDescriptorUpdate);

  return tex_ent;
}

bool tb_is_texture_ready(ecs_world_t *ecs, TbTexture tex) {
  return ecs_has(ecs, tex, TbTextureLoaded) &&
         ecs_has(ecs, tex, TbTextureComponent) &&
         ecs_has(ecs, tex, TbDescriptorReady);
}

TbTexture tb_get_default_color_tex(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get(ecs, TbTextureCtx);
  return ctx->default_color_tex;
}
TbTexture tb_get_default_normal_tex(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get(ecs, TbTextureCtx);
  return ctx->default_normal_tex;
}
TbTexture tb_get_default_metal_rough_tex(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get(ecs, TbTextureCtx);
  return ctx->default_metal_rough_tex;
}
TbTexture tb_get_brdf_tex(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get(ecs, TbTextureCtx);
  return ctx->brdf_tex;
}
