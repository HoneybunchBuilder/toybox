#include "texturesystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "rendersystem.h"
#include "tbktx.h"
#include "world.h"

uint32_t find_tex_by_id(TextureSystem *self, TbTextureId id) {
  for (uint32_t i = 0; i < self->tex_count; ++i) {
    if (self->tex_ids[i] == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

typedef struct KTX2IterData {
  VkBuffer buffer;
  VkImage image;
  BufferImageCopy *uploads;
  uint64_t offset;
} KTX2IterData;

static ktx_error_code_e iterate_ktx2_levels(int32_t mip_level, int32_t face,
                                            int32_t width, int32_t height,
                                            int32_t depth,
                                            uint64_t face_lod_size,
                                            void *pixels, void *userdata) {
  (void)pixels;
  KTX2IterData *user_data = (KTX2IterData *)userdata;

  user_data->uploads[mip_level] = (BufferImageCopy){
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
  TbTextureId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)name, SDL_strlen(name));
  return id;
}

uint32_t alloc_tex(TextureSystem *self) {
  // Resize collection if necessary
  const uint32_t new_count = self->tex_count + 1;
  if (new_count > self->tex_max) {
    // Re-allocate space for textures
    const uint32_t new_max = new_count * 2;

    Allocator alloc = self->std_alloc;

    self->tex_ids =
        tb_realloc_nm_tp(alloc, self->tex_ids, new_max, TbTextureId);
    self->tex_host_buffers =
        tb_realloc_nm_tp(alloc, self->tex_host_buffers, new_max, TbHostBuffer);
    self->tex_gpu_images =
        tb_realloc_nm_tp(alloc, self->tex_gpu_images, new_max, TbImage);
    self->tex_image_views =
        tb_realloc_nm_tp(alloc, self->tex_image_views, new_max, VkImageView);
    self->tex_ref_counts =
        tb_realloc_nm_tp(alloc, self->tex_ref_counts, new_max, uint32_t);

    self->tex_max = new_max;
  }

  const uint32_t index = self->tex_count;
  // Must initialize this or it could be garbage
  self->tex_ref_counts[index] = 0;
  self->tex_count++;
  return index;
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

TbTextureId tb_tex_system_create_texture_ktx2(TextureSystem *self,
                                              const char *path,
                                              const char *name,
                                              ktxTexture2 *ktx) {
  TbTextureId id = calc_tex_id(path, name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    self->tex_ref_counts[index]++;
    return id;
  }

  // If texture wasn't found, load it now
  VkResult err = VK_SUCCESS;
  RenderSystem *render_system = self->render_system;

  index = alloc_tex(self);

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
                    "Not expecting to have to generate mips", InvalidTextureId);

    // Get host buffer
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = host_buffer_size,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      };
      err = tb_rnd_sys_alloc_host_buffer(render_system, &create_info, name,
                                         &self->tex_host_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate host buffer for texture",
                      InvalidTextureId);

      // Copy data to the host buffer
      SDL_memcpy(self->tex_host_buffers[index].ptr, ktx->pData,
                 host_buffer_size);
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
          .imageType = get_ktx2_image_type(ktx),
          .mipLevels = mip_levels,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      };
      err = tb_rnd_sys_alloc_gpu_image(render_system, &create_info, name,
                                       &self->tex_gpu_images[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture",
                      InvalidTextureId);
    }

    // Create image view
    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = self->tex_gpu_images[index].image,
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
      SDL_snprintf(view_name, 100, "%s Image View", name);
      err = tb_rnd_create_image_view(render_system, &create_info, view_name,
                                     &self->tex_image_views[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                      InvalidTextureId);
    }

    // Issue upload
    {
      BufferImageCopy *uploads =
          tb_alloc_nm_tp(render_system->tmp_alloc, mip_levels, BufferImageCopy);

      KTX2IterData iter_data = {
          .buffer = self->tex_host_buffers[index].buffer,
          .image = self->tex_gpu_images[index].image,
          .offset = self->tex_host_buffers[index].offset,
          .uploads = uploads,
      };

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#endif
      // Iterate over texture levels to fill out upload requests
      ktxTexture_IterateLevels(ktx, iterate_ktx2_levels, &iter_data);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif

      // Will handle transitioning the image's layout to shader read only
      tb_rnd_upload_buffer_to_image(render_system, uploads, mip_levels);
    }
  }

  self->tex_ids[index] = id;
  self->tex_ref_counts[index]++;

  return id;
}

bool create_texture_system(TextureSystem *self,
                           const TextureSystemDescriptor *desc,
                           uint32_t system_dep_count,
                           System *const *system_deps) {
  // Find the render system
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which textures depend on",
                  false);

  *self = (TextureSystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .render_system = render_system,
      .default_color_tex = InvalidTextureId,
      .default_normal_tex = InvalidTextureId,
      .default_metal_rough_tex = InvalidTextureId,
  };

  {
    // All white 2x2 RGBA image
    const uint8_t pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    self->default_color_tex = tb_tex_system_create_texture(
        self, "", "Default Color Texture", TB_TEX_USAGE_COLOR, 2, 2, pixels,
        sizeof(pixels));
  }
  {
    // 2x2 blank normal image
    const uint8_t pixels[] = {
        0x7E, 0x7E, 0xFF, 255, 0x7E, 0x7E, 0xFF, 255,
        0x7E, 0x7E, 0xFF, 255, 0x7E, 0x7E, 0xFF, 255,
    };
    self->default_normal_tex = tb_tex_system_create_texture(
        self, "", "Default Normal Texture", TB_TEX_USAGE_NORMAL, 2, 2, pixels,
        sizeof(pixels));
  }
  {
    // 2x2 blank metal rough image
    const uint8_t pixels[] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    self->default_metal_rough_tex = tb_tex_system_create_texture(
        self, "", "Default Metal Rough Texture", TB_TEX_USAGE_METAL_ROUGH, 2, 2,
        pixels, sizeof(pixels));
  }

  // Load BRDF LUT texture
  {
    const char *path = ASSET_PREFIX "textures/brdf.ktx2";

    ktxTexture2 *ktx = NULL;
    {
      ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

      ktx_error_code_e err = ktxTexture2_CreateFromNamedFile(path, flags, &ktx);
      TB_CHECK_RETURN(err == KTX_SUCCESS,
                      "Failed to create KTX texture from memory",
                      InvalidTextureId);

      bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
      if (needs_transcoding) {
        // TODO: pre-calculate the best format for the platform
        err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
        TB_CHECK_RETURN(err == KTX_SUCCESS, "Failed transcode basis texture",
                        InvalidTextureId);
      }
    }

    // Create texture from ktx2 texture
    self->brdf_tex =
        tb_tex_system_create_texture_ktx2(self, path, "BRDF LUT", ktx);
  }

  return true;
}

void destroy_texture_system(TextureSystem *self) {
  tb_tex_system_release_texture_ref(self, self->default_metal_rough_tex);
  tb_tex_system_release_texture_ref(self, self->default_normal_tex);
  tb_tex_system_release_texture_ref(self, self->default_color_tex);
  tb_tex_system_release_texture_ref(self, self->brdf_tex);

  for (uint32_t i = 0; i < self->tex_count; ++i) {
    if (self->tex_ref_counts[i] != 0) {
      TB_CHECK(false, "Leaking textures");
    }
  }

  *self = (TextureSystem){
      .default_color_tex = InvalidTextureId,
      .default_normal_tex = InvalidTextureId,
      .default_metal_rough_tex = InvalidTextureId,
  };
}

void tick_texture_system(TextureSystem *self, const SystemInput *input,
                         SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(texture, TextureSystem, TextureSystemDescriptor)

void tb_texture_system_descriptor(SystemDescriptor *desc,
                                  const TextureSystemDescriptor *tex_desc) {
  *desc = (SystemDescriptor){
      .name = "Texture",
      .size = sizeof(TextureSystem),
      .id = TextureSystemId,
      .desc = (InternalDescriptor)tex_desc,
      .dep_count = 0,
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_texture_system,
      .destroy = tb_destroy_texture_system,
      .tick = tb_tick_texture_system,
  };
}

VkImageView tb_tex_system_get_image_view(TextureSystem *self, TbTextureId tex) {
  const uint32_t index = find_tex_by_id(self, tex);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32,
                  "Failed to find texture by id when retrieving image view",
                  VK_NULL_HANDLE);

  return self->tex_image_views[index];
}

TbTextureId tb_tex_system_create_texture(TextureSystem *self, const char *path,
                                         const char *name, TbTextureUsage usage,
                                         uint32_t width, uint32_t height,
                                         const uint8_t *pixels, uint64_t size) {
  TbTextureId id = calc_tex_id(path, name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    self->tex_ref_counts[index]++;
    return id;
  }

  // If texture wasn't found, load it now
  VkResult err = VK_SUCCESS;
  RenderSystem *render_system = self->render_system;

  index = alloc_tex(self);

  // Load texture
  {
    // Get host buffer
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = size,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      };
      err = tb_rnd_sys_alloc_host_buffer(render_system, &create_info, name,
                                         &self->tex_host_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate host buffer for texture",
                      InvalidTextureId);

      // Copy data to the host buffer
      SDL_memcpy(self->tex_host_buffers[index].ptr, pixels, size);
    }

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
      err = tb_rnd_sys_alloc_gpu_image(render_system, &create_info, name,
                                       &self->tex_gpu_images[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture",
                      InvalidTextureId);
    }

    // Create image view
    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = self->tex_gpu_images[index].image,
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
      SDL_snprintf(view_name, 100, "%s Image View", name);
      err = tb_rnd_create_image_view(render_system, &create_info, view_name,
                                     &self->tex_image_views[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                      InvalidTextureId);
    }

    // Issue upload
    {
      BufferImageCopy upload = {
          .src = self->tex_host_buffers[index].buffer,
          .dst = self->tex_gpu_images[index].image,
          .region =
              {
                  .bufferOffset = self->tex_host_buffers[index].offset,
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
      tb_rnd_upload_buffer_to_image(render_system, &upload, 1);
    }
  }

  self->tex_ids[index] = id;
  self->tex_ref_counts[index]++;

  return id;
}

TbTextureId tb_tex_system_alloc_texture(TextureSystem *self, const char *name,
                                        const VkImageCreateInfo *create_info) {
  TbTextureId id = calc_tex_id("", name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    self->tex_ref_counts[index]++;
    return id;
  }

  VkResult err = VK_SUCCESS;
  RenderSystem *render_system = self->render_system;

  index = alloc_tex(self);

  {
    const VkFormat format = create_info->format;
    // Allocate image
    {
      err = tb_rnd_sys_alloc_gpu_image(render_system, create_info, name,
                                       &self->tex_gpu_images[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture",
                      InvalidTextureId);
    }

    // Create image view
    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = self->tex_gpu_images[index].image,
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
      SDL_snprintf(view_name, 100, "%s Image View", name);
      err = tb_rnd_create_image_view(render_system, &create_info, view_name,
                                     &self->tex_image_views[index]);
      TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                      InvalidTextureId);
    }
  }

  self->tex_ids[index] = id;
  self->tex_ref_counts[index]++;

  return id;
}

TbTextureId tb_tex_system_import_texture(TextureSystem *self, const char *name,
                                         const TbImage *image,
                                         VkFormat format) {
  TbTextureId id = calc_tex_id("", name);
  uint32_t index = find_tex_by_id(self, id);

  // Index was found, we can just inc the ref count and early out
  if (index != SDL_MAX_UINT32) {
    self->tex_ref_counts[index]++;
    return id;
  }

  VkResult err = VK_SUCCESS;
  RenderSystem *render_system = self->render_system;

  index = alloc_tex(self);

  // Create image view
  {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = self->tex_gpu_images[index].image,
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
    SDL_snprintf(view_name, 100, "%s Image View", name);
    err = tb_rnd_create_image_view(render_system, &create_info, view_name,
                                   &self->tex_image_views[index]);
    TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                    InvalidTextureId);
  }

  self->tex_ids[index] = id;
  self->tex_ref_counts[index]++;
  self->tex_gpu_images[index] = *image;

  return id;
}

TbTextureId tb_tex_system_load_texture(TextureSystem *self, const char *path,
                                       const char *name,
                                       const cgltf_texture *texture) {
  // Must use basisu image
  TB_CHECK_RETURN(texture->has_basisu, "Expecting basisu image",
                  InvalidTextureId);

  const cgltf_image *image = texture->basisu_image;
  const cgltf_buffer_view *image_view = image->buffer_view;
  const cgltf_buffer *image_data = image_view->buffer;

  // Points to some jpg/png whatever image format data
  uint8_t *raw_data = (uint8_t *)(image_data->data) + image_view->offset;
  const int32_t raw_size = (int32_t)image_view->size;

  TB_CHECK_RETURN(image->buffer_view->buffer->uri == NULL,
                  "Not setup to load data from uri", InvalidTextureId);

  // Load the ktx texture
  ktxTexture2 *ktx = NULL;
  {
    ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;

    ktx_error_code_e err =
        ktxTexture2_CreateFromMemory(raw_data, raw_size, flags, &ktx);
    TB_CHECK_RETURN(err == KTX_SUCCESS,
                    "Failed to create KTX texture from memory",
                    InvalidTextureId);

    bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
    if (needs_transcoding) {
      // TODO: pre-calculate the best format for the platform
      err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
      TB_CHECK_RETURN(err == KTX_SUCCESS, "Failed transcode basis texture",
                      InvalidTextureId);
    }
  }

  // Create texture from ktx2 texture
  TbTextureId tex = tb_tex_system_create_texture_ktx2(self, path, name, ktx);
  return tex;
}

bool tb_tex_system_take_tex_ref(TextureSystem *self, TbTextureId id) {
  uint32_t index = find_tex_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find texture", false);

  self->tex_ref_counts[index]++;

  return true;
}

void tb_tex_system_release_texture_ref(TextureSystem *self, TbTextureId tex) {
  const uint32_t index = find_tex_by_id(self, tex);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find texture to release");
    return;
  }

  if (self->tex_ref_counts[index] == 0) {
    TB_CHECK(false, "Tried to release reference to texture with 0 ref count");
    return;
  }

  self->tex_ref_counts[index]--;

  if (self->tex_ref_counts[index] == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->render_system->vma_alloc;

    TbHostBuffer *host_buf = &self->tex_host_buffers[index];
    TbImage *gpu_img = &self->tex_gpu_images[index];
    VkImageView *view = &self->tex_image_views[index];

    vmaUnmapMemory(vma_alloc, host_buf->alloc);

    vmaDestroyBuffer(vma_alloc, host_buf->buffer, host_buf->alloc);
    vmaDestroyImage(vma_alloc, gpu_img->image, gpu_img->alloc);
    vkDestroyImageView(self->render_system->render_thread->device, *view,
                       &self->render_system->vk_host_alloc_cb);

    *host_buf = (TbHostBuffer){0};
    *gpu_img = (TbImage){0};
    *view = VK_NULL_HANDLE;
  }
}
