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
  TbFrameDescriptorPoolList frame_pools;
} TbTexture2Ctx;
ECS_COMPONENT_DECLARE(TbTexture2Ctx);

typedef struct TbTexture2Comp {
  uint32_t ref_count;
  TbHostBuffer host_buffer;
  TbImage gpu_image;
  VkImageView image_view;
} TbTexture2Comp;
ECS_COMPONENT_DECLARE(TbTexture2Comp);

// Describes the creation of a texture from a cgltf texture pointer
typedef struct TbTextureGLTFLoadRequest {
  const char *path;
  const char *mat_name; // TODO: Should be an entity id
} TbTextureGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbTextureGLTFLoadRequest);

ECS_TAG_DECLARE(TbTextureLoaded);
ECS_TAG_DECLARE(TbTextureReady);
ECS_TAG_DECLARE(TbNeedTexDescUpdate);

// Internals

typedef struct TbLoadTexture2Args {
  ecs_world_t *ecs;
  TbTexture2 tex;
  TbTaskScheduler enki;
  TbRenderSystem *rnd_sys;
  TbPinnedTask loaded_task;
  const char *path;
  const char *mat_name; // TODO: Should be an entity id
  TbTextureUsage usage;
} TbLoadTexture2Args;

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

TbTexture2Comp tb_load_ktx_texture2(TbRenderSystem *rnd_sys, const char *name,
                                    ktxTexture2 *ktx) {

  size_t host_buffer_size = ktx->dataSize;
  uint32_t width = ktx->baseWidth;
  uint32_t height = ktx->baseHeight;
  uint32_t depth = ktx->baseDepth;
  uint32_t layers = ktx->numLayers;
  uint32_t mip_levels = ktx->numLevels;
  VkFormat format = (VkFormat)ktx->vkFormat;

  TB_CHECK(ktx->generateMipmaps == false,
           "Not expecting to have to generate mips");

  TbTexture2Comp texture = {0};

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

TbTexture2Comp tb_load_texture_2(TbRenderSystem *rnd_sys, const char *name,
                                 const cgltf_texture *texture) {
  TracyCZoneN(ctx, "Load Texture 2", true);
  // Must use basisu image
  TB_CHECK(texture->has_basisu, "Expecting basisu image");

  const cgltf_image *image = texture->basisu_image;
  const cgltf_buffer_view *image_view = image->buffer_view;
  const cgltf_buffer *image_data = image_view->buffer;

  // Points to some jpg/png whatever image format data
  uint8_t *raw_data = (uint8_t *)(image_data->data) + image_view->offset;
  const int32_t raw_size = (int32_t)image_view->size;

  TB_CHECK(image->buffer_view->buffer->uri == NULL,
           "Not setup to load data from uri");

  // Load the ktx texture
  ktxTexture2 *ktx = NULL;
  {
    ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

    ktx_error_code_e err =
        ktxTexture2_CreateFromMemory(raw_data, raw_size, flags, &ktx);
    TB_CHECK(err == KTX_SUCCESS, "Failed to create KTX texture from memory");

    bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
    if (needs_transcoding) {
      // TODO: pre-calculate the best format for the platform
      err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
      TB_CHECK(err == KTX_SUCCESS, "Failed transcode basis texture");
    }
  }

  // Create texture from ktx2 texture
  TbTexture2Comp tex = tb_load_ktx_texture2(rnd_sys, name, ktx);
  TracyCZoneEnd(ctx);
  return tex;
}

typedef struct TbTextureLoadedArgs {
  ecs_world_t *ecs;
  TbTexture2 tex;
  TbTexture2Comp comp;
} TbTextureLoadedArgs;

void tb_texture_loaded2(const void *args) {
  tb_auto loaded_args = (const TbTextureLoadedArgs *)args;
  tb_auto ecs = loaded_args->ecs;
  tb_auto tex = loaded_args->tex;
  if (tex != 0) {
    ecs_add(ecs, tex, TbTextureLoaded);
    ecs_set_ptr(ecs, tex, TbTexture2Comp, &loaded_args->comp);
  } else {
    TB_CHECK(false, "Texture load failed. Do we need to retry?");
  }
}

void tb_load_texture_task(const void *args) {
  TracyCZoneN(ctx, "Load Texture Task", true);
  tb_auto load_args = (const TbLoadTexture2Args *)args;
  tb_auto rnd_sys = load_args->rnd_sys;
  tb_auto path = load_args->path;
  tb_auto mat_name = load_args->mat_name;
  tb_auto usage = load_args->usage;

  TbTexture2 tex = load_args->tex;

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

  TbTexture2Comp tex_comp = {0};
  if (tex != 0) {
    tex_comp = tb_load_texture_2(rnd_sys, image_name, texture);
  }

  // Launch pinned task to handle loading signals on main thread
  TbTextureLoadedArgs loaded_args = {
      .ecs = load_args->ecs,
      .tex = tex,
      .comp = tex_comp,
  };
  tb_launch_pinned_task_args(load_args->enki, load_args->loaded_task,
                             &loaded_args, sizeof(TbTextureLoadedArgs));
  TracyCZoneEnd(ctx);
}

// Systems

void tb_queue_tex_loads(ecs_iter_t *it) {
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto reqs = ecs_field(it, TbTextureGLTFLoadRequest, 2);
  tb_auto usages = ecs_field(it, TbTextureUsage, 3);

  tb_auto rnd_sys = ecs_singleton_get_mut(it->world, TbRenderSystem);

  // TODO: Time slice the time spent creating tasks
  // Iterate texture load tasks
  for (int32_t i = 0; i < it->count; ++i) {
    TbTexture2 ent = it->entities[i];
    tb_auto req = reqs[i];
    tb_auto usage = usages[i];

    // This pinned task will be launched by the loading task
    TbPinnedTask loaded_task =
        tb_create_pinned_task(enki, tb_texture_loaded2, NULL, 0);

    TbLoadTexture2Args args = {
        .ecs = it->world,
        .tex = ent,
        .enki = enki,
        .rnd_sys = rnd_sys,
        .loaded_task = loaded_task,
        .path = req.path,
        .mat_name = req.mat_name,
        .usage = usage,
    };
    TbTask load_task = tb_async_task(enki, tb_load_texture_task, &args,
                                     sizeof(TbLoadTexture2Args));
    // Apply task component to texture entity
    ecs_set(it->world, ent, TbTask, {load_task});

    // Remove load request as it has now been enqueued to the task system
    ecs_remove(it->world, ent, TbTextureGLTFLoadRequest);
  }
}

// Toybox Glue

void tb_register_texture2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbTexture2Ctx);
  ECS_COMPONENT_DEFINE(ecs, TbTextureGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbTexture2Comp);
  ECS_COMPONENT_DEFINE(ecs, TbTextureUsage);
  ECS_TAG_DEFINE(ecs, TbTextureLoaded);
  ECS_TAG_DEFINE(ecs, TbTextureReady);
  ECS_TAG_DEFINE(ecs, TbNeedTexDescUpdate);

  ECS_SYSTEM(ecs, tb_queue_tex_loads, EcsPreUpdate,
             TbTaskScheduler(TbTaskScheduler), [in] TbTextureGLTFLoadRequest,
             [in] TbTextureUsage);

  // TODO: Init ctx specific resources
  TbTexture2Ctx ctx = {0};
  ecs_singleton_set_ptr(ecs, TbTexture2Ctx, &ctx);
}

void tb_unregister_texture2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ecs_singleton_remove(ecs, TbTexture2Ctx);
}

TB_REGISTER_SYS(tb, texture2, TB_TEX_SYS_PRIO)

// Public API

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
  // Or do we create a task pinned on the loading thread?
  ecs_set(ecs, tex_ent, TbTextureGLTFLoadRequest, {path, mat_name});
  ecs_set(ecs, tex_ent, TbTextureUsage, {usage});

  return tex_ent;
}

bool tb_is_tex_loaded(ecs_world_t *ecs, TbTexture2 tex) {
  return ecs_has(ecs, tex, TbTextureLoaded);
}
