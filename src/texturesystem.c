#include "texturesystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "rendersystem.h"
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

  return true;
}

void destroy_texture_system(TextureSystem *self) {
  tb_tex_system_release_texture_ref(self, self->default_metal_rough_tex);
  tb_tex_system_release_texture_ref(self, self->default_normal_tex);
  tb_tex_system_release_texture_ref(self, self->default_color_tex);

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
  desc->name = "Mesh";
  desc->size = sizeof(TextureSystem);
  desc->id = TextureSystemId;
  desc->desc = (InternalDescriptor)tex_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 0;
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_texture_system;
  desc->destroy = tb_destroy_texture_system;
  desc->tick = tb_tick_texture_system;
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
  // Hash the texture's path and name to get the id
  TbTextureId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)name, SDL_strlen(name));

  uint32_t index = find_tex_by_id(self, id);

  // If texture wasn't found, load it now
  if (index == SDL_MAX_UINT32) {

    VkResult err = VK_SUCCESS;

    RenderSystem *render_system = self->render_system;

    // Resize collection if necessary
    {
      const uint32_t new_count = self->tex_count + 1;
      if (new_count > self->tex_max) {
        // Re-allocate space for meshes
        const uint32_t new_max = new_count * 2;

        Allocator alloc = self->std_alloc;

        self->tex_ids =
            tb_realloc_nm_tp(alloc, self->tex_ids, new_max, TbTextureId);
        self->tex_host_buffers = tb_realloc_nm_tp(alloc, self->tex_host_buffers,
                                                  new_max, TbHostBuffer);
        self->tex_gpu_images =
            tb_realloc_nm_tp(alloc, self->tex_gpu_images, new_max, TbImage);
        self->tex_image_views = tb_realloc_nm_tp(alloc, self->tex_image_views,
                                                 new_max, VkImageView);
        self->tex_ref_counts =
            tb_realloc_nm_tp(alloc, self->tex_ref_counts, new_max, uint32_t);

        self->tex_max = new_max;
      }

      index = self->tex_count;
    }

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
            .usage =
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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
        err = vkCreateImageView(self->render_system->render_thread->device,
                                &create_info,
                                &self->render_system->vk_host_alloc_cb,
                                &self->tex_image_views[index]);
        TB_VK_CHECK_RET(err, "Failed to allocate image view for texture",
                        InvalidTextureId);

        const uint32_t view_name_max = 100;
        char view_name[view_name_max];
        SDL_snprintf(view_name, view_name_max, "%s Image View", name);
        SET_VK_NAME(self->render_system->render_thread->device,
                    self->tex_image_views[index], VK_OBJECT_TYPE_IMAGE_VIEW,
                    view_name);
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
        };
        tb_rnd_upload_buffer_to_image(render_system, &upload, 1);
      }
    }

    self->tex_ids[index] = id;
    self->tex_count++;
  }

  self->tex_ref_counts[index]++;

  return id;
}

SDL_Surface *parse_and_transform_image2(uint8_t *data, int32_t size) {
  SDL_RWops *ops = SDL_RWFromMem((void *)data, size);
  SDL_Surface *img = IMG_Load_RW(ops, 0);
  TB_CHECK_RETURN(img, "Failed to load image", NULL);

  SDL_PixelFormat *opt_fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

  SDL_Surface *opt_img = SDL_ConvertSurface(img, opt_fmt, 0);
  SDL_FreeSurface(img);

  return opt_img;
}

TbTextureId tb_tex_system_load_texture(TextureSystem *self, const char *path,
                                       TbTextureUsage usage,
                                       const cgltf_texture *texture) {
  const cgltf_image *image = texture->image;
  const char *name = image->name;
  const cgltf_buffer_view *image_view = image->buffer_view;
  const cgltf_buffer *image_data = image_view->buffer;

  // Points to some jpg/png whatever image format data
  uint8_t *raw_data = (uint8_t *)(image_data->data) + image_view->offset;
  const int32_t raw_size = (int32_t)image_view->size;

  TB_CHECK_RETURN(image->buffer_view->buffer->uri == NULL,
                  "Not setup to load data from uri", false);

  // Transform the buffer to raw pixels
  SDL_Surface *surf = parse_and_transform_image2(raw_data, raw_size);
  uint32_t width = (uint32_t)surf->w;
  uint32_t height = (uint32_t)surf->h;
  uint8_t *pixels = surf->pixels;
  uint64_t size = (uint32_t)surf->pitch * height;

  TbTextureId tex = tb_tex_system_create_texture(self, path, name, usage, width,
                                                 height, pixels, size);

  SDL_FreeSurface(surf);

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
