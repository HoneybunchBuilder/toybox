#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define TextureSystemId 0xBADDCAFE

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct cgltf_texture cgltf_texture;
typedef struct VkImageView_T *VkImageView;

typedef uint64_t TbTextureId;
static const TbTextureId InvalidTextureId = SDL_MAX_UINT64;

typedef struct TextureSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} TextureSystemDescriptor;

typedef struct TextureSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  uint32_t tex_count;
  TbTextureId *tex_ids;
  TbBuffer *tex_host_buffers;
  TbImage *tex_gpu_images;
  VkImageView *tex_image_views;
  uint32_t *tex_ref_counts;
  uint32_t tex_max;

  TbTextureId default_color_tex;
  TbTextureId default_normal_tex;
  TbTextureId default_metal_rough_tex;
} TextureSystem;

void tb_texture_system_descriptor(SystemDescriptor *desc,
                                  const TextureSystemDescriptor *tex_desc);

VkImageView tb_tex_system_get_image_view(TextureSystem *self, TbTextureId tex);

TbTextureId tb_tex_system_create_texture(TextureSystem *self, const char *path,
                                         const char *name);
TbTextureId tb_tex_system_load_texture(TextureSystem *self, const char *path,
                                       const cgltf_texture *texture);
void tb_mat_system_release_texture_ref(TextureSystem *self, TbTextureId tex);
