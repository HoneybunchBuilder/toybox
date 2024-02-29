#include "texturesystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tbktx.h"
#include "world.h"

#include <flecs.h>

ECS_COMPONENT_DECLARE(TbTextureSystem);

typedef struct TbTexture {
  TbTextureId id;
  uint32_t ref_count;
  TbHostBuffer host_buffer;
  TbImage gpu_image;
  VkImageView image_view;
} TBTexture;

void tb_register_texture_sys(TbWorld *world);
void tb_unregister_texture_sys(TbWorld *world);

TB_REGISTER_SYS(tb, texture, TB_TEX_SYS_PRIO)

uint32_t find_tex_by_id(TbTextureSystem *self, TbTextureId id) {
  TB_DYN_ARR_FOREACH(self->textures, i) {
    if (TB_DYN_ARR_AT(self->textures, i).id == id) {
      return i;
    }
  }
  return SDL_MAX_UINT32;
}

typedef struct KTX2IterData {
  VkBuffer buffer;
  VkImage image;
  TbBufferImageCopy *uploads;
  uint64_t offset;
} KTX2IterData;

static ktx_error_code_e iterate_ktx2_levels(int32_t mip_level, int32_t face,
                                            int32_t width, int32_t height,
                                            int32_t depth,
                                            uint64_t face_lod_size,
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

TbTextureId calc_tex_id(const char *path, const char *name) {
  TbTextureId id = tb_hash(0, (const uint8_t *)path, SDL_strlen(path));
  id = tb_hash(id, (const uint8_t *)name, SDL_strlen(name));
  return id;
}

TbTexture *alloc_tex(TbTextureSystem *self, TbTextureId id) {
  uint32_t index = TB_DYN_ARR_SIZE(self->textures);
  TbTexture tex = {.id = id, .ref_count = 1};
  TB_DYN_ARR_APPEND(self->textures, tex);
  return &TB_DYN_ARR_AT(self->textures, index);
}

static VkImageType get_ktx2_image_type(const ktxTexture2 *t) {
  return (VkImageType)(t->numDimensions - 1);
}

static VkImageViewType get_ktx2_image_view_type(const ktxTexture2 *t) {
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

TbTextureId tb_tex_system_create_texture_ktx2(TbTextureSystem *self,
                                              const char *path,
                                              const char *name,
                                              ktxTexture2 *ktx) {
  TbTextureId id = calc_tex_id(path, name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    tb_tex_system_take_tex_ref(self, id);
    return id;
  }

  // If texture wasn't found, load it now
  VkResult err = VK_SUCCESS;
  TbRenderSystem *rnd_sys = self->rnd_sys;

  TbTexture *texture = alloc_tex(self, id);

  // Load texture
  {
    size_t host_buffer_size = ktx->dataSize;
    uint32_t width = ktx->baseWidth;
    uint32_t height = ktx->baseHeight;
    uint32_t depth = ktx->baseDepth;
    uint32_t layers = ktx->numLayers;
    uint32_t mip_levels = ktx->numLevels;
    VkFormat format = (VkFormat)ktx->vkFormat;

    TB_CHECK_RETURN(ktx->generateMipmaps == false,
                    "Not expecting to have to generate mips",
                    TbInvalidTextureId);

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
      err = tb_rnd_sys_create_gpu_image(rnd_sys, ktx->pData, host_buffer_size,
                                        &create_info, name, &texture->gpu_image,
                                        &texture->host_buffer);
      TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture",
                      TbInvalidTextureId);
    }

    // Create image view
    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = texture->gpu_image.image,
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
      err = tb_rnd_create_image_view(rnd_sys, &create_info, view_name,
                                     &texture->image_view);
      TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                      TbInvalidTextureId);
    }

    // Issue uploads
    {
      TbBufferImageCopy *uploads =
          tb_alloc_nm_tp(rnd_sys->tmp_alloc, mip_levels, TbBufferImageCopy);

      KTX2IterData iter_data = {
          .buffer = texture->host_buffer.buffer,
          .image = texture->gpu_image.image,
          .offset = texture->host_buffer.offset,
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
  }

  return id;
}

TbTextureSystem create_texture_system(TbAllocator gp_alloc,
                                      TbAllocator tmp_alloc,
                                      TbRenderSystem *rnd_sys) {
  TbTextureSystem sys = {
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
      .rnd_sys = rnd_sys,
      .default_color_tex = TbInvalidTextureId,
      .default_normal_tex = TbInvalidTextureId,
      .default_metal_rough_tex = TbInvalidTextureId,
  };

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
                             &sys.set_layout);
  }

  // Init textures dyn array
  TB_DYN_ARR_RESET(sys.textures, sys.gp_alloc, 4);

  // All white 2x2 RGBA image
  {
    const uint8_t pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    sys.default_color_tex = tb_tex_system_create_texture(
        &sys, "", "Default Color Texture", TB_TEX_USAGE_COLOR, 2, 2, pixels,
        sizeof(pixels));
  }
  // 2x2 blank normal image
  {
    const uint8_t pixels[] = {
        0x7E, 0x7E, 0xFF, 255, 0x7E, 0x7E, 0xFF, 255,
        0x7E, 0x7E, 0xFF, 255, 0x7E, 0x7E, 0xFF, 255,
    };
    sys.default_normal_tex = tb_tex_system_create_texture(
        &sys, "", "Default Normal Texture", TB_TEX_USAGE_NORMAL, 2, 2, pixels,
        sizeof(pixels));
  }
  // 2x2 blank metal rough image
  {
    const uint8_t pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    sys.default_metal_rough_tex = tb_tex_system_create_texture(
        &sys, "", "Default Metal Rough Texture", TB_TEX_USAGE_METAL_ROUGH, 2, 2,
        pixels, sizeof(pixels));
  }
  // Load BRDF LUT texture
  {
    const char *path = ASSET_PREFIX "textures/brdf.ktx2";

    ktxTexture2 *ktx = NULL;
    {
      ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

      // We need to open this file with SDL_RWops because on a platform like
      // android where the asset lives in package storage, this is the best way
      // to actually open the file you're looking for
      SDL_RWops *tex_file = SDL_RWFromFile(path, "rb");
      size_t tex_size = SDL_RWsize(tex_file);
      // We never free this since the BRDF LUT texture is a once time
      // importance
      // We *could* construct a KTX stream that directly reads the file
      // without reading to memory first but that's a lot of work for a single
      // texture
      uint8_t *tex_data = tb_alloc(sys.gp_alloc, tex_size);
      SDL_RWread(tex_file, (void *)tex_data, tex_size);
      SDL_RWclose(tex_file);

      ktx_error_code_e err =
          ktxTexture2_CreateFromMemory(tex_data, tex_size, flags, &ktx);
      TB_CHECK(err == KTX_SUCCESS, "Failed to create KTX texture from memory");

      bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
      if (needs_transcoding) {
        // TODO: pre-calculate the best format for the platform
        err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
        TB_CHECK(err == KTX_SUCCESS, "Failed transcode basis texture");
      }
    }

    // Create texture from ktx2 texture
    sys.brdf_tex =
        tb_tex_system_create_texture_ktx2(&sys, path, "BRDF LUT", ktx);
  }

  return sys;
}

void destroy_texture_system(TbTextureSystem *self) {
  tb_tex_system_release_texture_ref(self, self->default_metal_rough_tex);
  tb_tex_system_release_texture_ref(self, self->default_normal_tex);
  tb_tex_system_release_texture_ref(self, self->default_color_tex);
  tb_tex_system_release_texture_ref(self, self->brdf_tex);

  TB_DYN_ARR_FOREACH(self->textures, i) {
    if (TB_DYN_ARR_AT(self->textures, i).ref_count != 0) {
      TB_CHECK(false, "Leaking textures");
    }
  }
  TB_DYN_ARR_DESTROY(self->textures);

  *self = (TbTextureSystem){
      .default_color_tex = TbInvalidTextureId,
      .default_normal_tex = TbInvalidTextureId,
      .default_metal_rough_tex = TbInvalidTextureId,
  };
}

void tick_texture_system(ecs_iter_t *it) {
  ecs_world_t *ecs = it->world;

  tb_auto tex_sys = ecs_singleton_get_mut(ecs, TbTextureSystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  const uint64_t incoming_tex_count = TB_DYN_ARR_SIZE(tex_sys->textures);
  if (incoming_tex_count > tex_sys->set_pool.capacity) {
    tex_sys->set_pool.capacity = incoming_tex_count + 128;
    const uint64_t desc_count = tex_sys->set_pool.capacity;

    // Re-create pool and allocate the one set that everything will be bound to
    {
      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
          .maxSets = tex_sys->set_pool.capacity,
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
          .pDescriptorCounts = (uint32_t[1]){incoming_tex_count},
      };
      tb_rnd_resize_desc_pool(rnd_sys, &create_info, &tex_sys->set_layout,
                              &alloc_info, &tex_sys->set_pool, 1);
    }
  } else {
    // Nothing to do :)
    return;
  }

  // Write all textures into the descriptor set table
  TB_DYN_ARR_OF(VkWriteDescriptorSet) writes = {0};
  {
    tb_auto tex_count = TB_DYN_ARR_SIZE(tex_sys->textures);
    TB_DYN_ARR_RESET(writes, tex_sys->tmp_alloc, tex_count);
    tb_auto image_info =
        tb_alloc_nm_tp(tex_sys->tmp_alloc, tex_count, VkDescriptorImageInfo);

    TB_DYN_ARR_FOREACH(tex_sys->textures, i) {
      tb_auto texture = &TB_DYN_ARR_AT(tex_sys->textures, i);

      image_info[i] = (VkDescriptorImageInfo){
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView = texture->image_view,
      };

      tb_auto write = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .dstSet = tb_tex_sys_get_set(tex_sys),
          .dstArrayElement = i,
          .pImageInfo = &image_info[i],
      };
      TB_DYN_ARR_APPEND(writes, write);
    }
  }
  tb_rnd_update_descriptors(rnd_sys, TB_DYN_ARR_SIZE(writes), writes.data);
}

void tb_register_texture_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbTextureSystem);

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  ecs_singleton_modified(ecs, TbRenderSystem);

  TbTextureSystem sys =
      create_texture_system(world->gp_alloc, world->tmp_alloc, rnd_sys);

  ECS_SYSTEM(ecs, tick_texture_system, EcsPreStore,
             TbTextureSystem(TbTextureSystem));

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbTextureSystem), TbTextureSystem, &sys);
}

void tb_unregister_texture_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  TbTextureSystem *sys = ecs_singleton_get_mut(ecs, TbTextureSystem);
  destroy_texture_system(sys);
  ecs_singleton_remove(ecs, TbTextureSystem);
}

VkDescriptorSet tb_tex_sys_get_set(TbTextureSystem *self) {
  return self->set_pool.sets[0];
}

uint32_t tb_tex_system_get_index(TbTextureSystem *self, TbTextureId tex) {
  const uint32_t index = find_tex_by_id(self, tex);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32,
                  "Failed to find texture by id when retrieving image index",
                  SDL_MAX_UINT32);
  return index;
}

VkImageView tb_tex_system_get_image_view(TbTextureSystem *self,
                                         TbTextureId tex) {
  const uint32_t index = find_tex_by_id(self, tex);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32,
                  "Failed to find texture by id when retrieving image view",
                  VK_NULL_HANDLE);

  return TB_DYN_ARR_AT(self->textures, index).image_view;
}

TbTextureId tb_tex_system_create_texture(TbTextureSystem *self,
                                         const char *path, const char *name,
                                         TbTextureUsage usage, uint32_t width,
                                         uint32_t height, const uint8_t *pixels,
                                         uint64_t size) {
  TbTextureId id = calc_tex_id(path, name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    tb_tex_system_take_tex_ref(self, id);
    return id;
  }

  // If texture wasn't found, load it now
  VkResult err = VK_SUCCESS;
  TbRenderSystem *rnd_sys = self->rnd_sys;

  TbTexture *texture = alloc_tex(self, id);

  // Load texture
  {
    // Determine format based on usage
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    if (usage == TB_TEX_USAGE_COLOR) {
      format = VK_FORMAT_R8G8B8A8_SRGB;
    } else if (usage == TB_TEX_USAGE_BRDF) {
      format = VK_FORMAT_R32G32_SFLOAT;
    }

    // Allocate gpu image
    {
      // TODO: Think about mip maps
      VkImageCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .arrayLayers = 1,
          .extent =
              (VkExtent3D){
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
          .format = format,
          .imageType = VK_IMAGE_TYPE_2D,
          .mipLevels = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      };
      err = tb_rnd_sys_create_gpu_image(rnd_sys, pixels, size, &create_info,
                                        name, &texture->gpu_image,
                                        &texture->host_buffer);
      TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture",
                      TbInvalidTextureId);
    }

    // Create image view
    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = texture->gpu_image.image,
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
                  1,
                  0,
                  1,
              },
      };
      char view_name[100] = {0};
      SDL_snprintf(view_name, 100, "%s Image TbView", name); // NOLINT
      err = tb_rnd_create_image_view(rnd_sys, &create_info, view_name,
                                     &texture->image_view);
      TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                      TbInvalidTextureId);
    }

    // Issue upload
    {
      TbBufferImageCopy upload = {
          .src = texture->host_buffer.buffer,
          .dst = texture->gpu_image.image,
          .region =
              {
                  .bufferOffset = texture->host_buffer.offset,
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
      tb_rnd_upload_buffer_to_image(rnd_sys, &upload, 1);
    }
  }

  return id;
}

TbTextureId tb_tex_system_alloc_texture(TbTextureSystem *self, const char *name,
                                        const VkImageCreateInfo *create_info) {
  TbTextureId id = calc_tex_id("", name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    tb_tex_system_take_tex_ref(self, id);
    return id;
  }

  VkResult err = VK_SUCCESS;
  TbRenderSystem *rnd_sys = self->rnd_sys;

  TbTexture *texture = alloc_tex(self, id);

  {
    const VkFormat format = create_info->format;
    // Allocate image
    {
      err = tb_rnd_sys_alloc_gpu_image(rnd_sys, create_info, 0, name,
                                       &texture->gpu_image);
      TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture",
                      TbInvalidTextureId);
    }

    // Create image view
    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = texture->gpu_image.image,
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
                  1,
                  0,
                  1,
              },
      };
      char view_name[100] = {0};
      SDL_snprintf(view_name, 100, "%s Image TbView", name); // NOLINT
      err = tb_rnd_create_image_view(rnd_sys, &create_info, view_name,
                                     &texture->image_view);
      TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                      TbInvalidTextureId);
    }
  }

  return id;
}

TbTextureId tb_tex_system_import_texture(TbTextureSystem *self,
                                         const char *name, const TbImage *image,
                                         VkFormat format) {
  TbTextureId id = calc_tex_id("", name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    tb_tex_system_take_tex_ref(self, id);
    return id;
  }

  VkResult err = VK_SUCCESS;
  TbRenderSystem *rnd_sys = self->rnd_sys;

  TbTexture *texture = alloc_tex(self, id);

  // Create image view
  {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->gpu_image.image,
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
                1,
                0,
                1,
            },
    };
    char view_name[100] = {0};
    SDL_snprintf(view_name, 100, "%s Image TbView", name); // NOLINT
    err = tb_rnd_create_image_view(rnd_sys, &create_info, view_name,
                                   &texture->image_view);
    TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                    TbInvalidTextureId);
  }

  texture->gpu_image = *image;

  return id;
}

TbTextureId tb_tex_system_load_texture(TbTextureSystem *self, const char *path,
                                       const char *name,
                                       const cgltf_texture *texture) {
  TracyCZoneN(ctx, "Load Texture", true);
  // Must use basisu image
  TB_CHECK_RETURN(texture->has_basisu, "Expecting basisu image",
                  TbInvalidTextureId);

  const cgltf_image *image = texture->basisu_image;
  const cgltf_buffer_view *image_view = image->buffer_view;
  const cgltf_buffer *image_data = image_view->buffer;

  // Points to some jpg/png whatever image format data
  uint8_t *raw_data = (uint8_t *)(image_data->data) + image_view->offset;
  const int32_t raw_size = (int32_t)image_view->size;

  TB_CHECK_RETURN(image->buffer_view->buffer->uri == NULL,
                  "Not setup to load data from uri", TbInvalidTextureId);

  // Load the ktx texture
  ktxTexture2 *ktx = NULL;
  {
    ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

    ktx_error_code_e err =
        ktxTexture2_CreateFromMemory(raw_data, raw_size, flags, &ktx);
    TB_CHECK_RETURN(err == KTX_SUCCESS,
                    "Failed to create KTX texture from memory",
                    TbInvalidTextureId);

    bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
    if (needs_transcoding) {
      // TODO: pre-calculate the best format for the platform
      err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
      TB_CHECK_RETURN(err == KTX_SUCCESS, "Failed transcode basis texture",
                      TbInvalidTextureId);
    }
  }

  // Create texture from ktx2 texture
  TbTextureId tex = tb_tex_system_create_texture_ktx2(self, path, name, ktx);
  TracyCZoneEnd(ctx);
  return tex;
}

bool tb_tex_system_take_tex_ref(TbTextureSystem *self, TbTextureId id) {
  uint32_t index = find_tex_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find texture", false);

  TB_DYN_ARR_AT(self->textures, index).ref_count++;
  return true;
}

void tb_tex_system_release_texture_ref(TbTextureSystem *self, TbTextureId tex) {
  const uint32_t index = find_tex_by_id(self, tex);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find texture to release");
    return;
  }

  TbTexture *texture = &TB_DYN_ARR_AT(self->textures, index);

  if (texture->ref_count == 0) {
    TB_CHECK(false, "Tried to release reference to texture with 0 ref count");
    return;
  }
  texture->ref_count--;

  if (texture->ref_count == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->rnd_sys->vma_alloc;

    TbHostBuffer *host_buf = &texture->host_buffer;
    TbImage *gpu_img = &texture->gpu_image;
    VkImageView *view = &texture->image_view;

    vmaUnmapMemory(vma_alloc, host_buf->alloc);

    vmaDestroyBuffer(vma_alloc, host_buf->buffer, host_buf->alloc);
    vmaDestroyImage(vma_alloc, gpu_img->image, gpu_img->alloc);
    vkDestroyImageView(self->rnd_sys->render_thread->device, *view,
                       &self->rnd_sys->vk_host_alloc_cb);

    *host_buf = (TbHostBuffer){0};
    *gpu_img = (TbImage){0};
    *view = VK_NULL_HANDLE;
  }
}
