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

  self->default_color_tex =
      tb_tex_system_create_texture(self, "", "Default Color Texture");
  self->default_normal_tex =
      tb_tex_system_create_texture(self, "", "Default Normal Texture");
  self->default_metal_rough_tex =
      tb_tex_system_create_texture(self, "", "Default Metal Rough Texture");

  return true;
}

void destroy_texture_system(TextureSystem *self) {
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

VkImageView tb_texture_system_get_image_view(TextureSystem *self,
                                             TbTextureId tex) {

  const uint32_t index = find_tex_by_id(self, tex);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32,
                  "Failed to find texture by id when retrieving image view",
                  VK_NULL_HANDLE);

  return self->tex_image_views[index];
}

TbTextureId tb_tex_system_create_texture(TextureSystem *self, const char *path,
                                         const char *name) {
  // Hash the texture's path and name to get the id
  TbTextureId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)name, SDL_strlen(name));

  const uint32_t index = find_tex_by_id(self, id);

  // If texture wasn't found, load it now
  if (index == SDL_MAX_UINT32) {
  }

  return id;
}

TbTextureId tb_tex_system_load_texture(TextureSystem *self, const char *path,
                                       const cgltf_texture *texture) {
  const char *name = texture->name;
  return tb_tex_system_create_texture(self, path, name);
}

void tb_mat_system_release_texture_ref(TextureSystem *self, TbTextureId tex) {
  const uint32_t index = find_tex_by_id(self, tex);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find texture to release");
    return;
  }

  if (self->tex_ref_counts[index] == 0) {
    TB_CHECK(false, "Tried to release reference to mesh with 0 ref count");
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
