#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "dynarray.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define TextureSystemId 0xBADDCAFE

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct cgltf_texture cgltf_texture;
typedef struct VkImageView_T *VkImageView;

typedef uint64_t TbTextureId;
static const TbTextureId InvalidTextureId = SDL_MAX_UINT64;

typedef enum TbTextureUsage {
  TB_TEX_USAGE_UNKNOWN = 0,
  TB_TEX_USAGE_COLOR,
  TB_TEX_USAGE_NORMAL,
  TB_TEX_USAGE_METAL_ROUGH,
  TB_TEX_USAGE_BRDF,
} TbTextureUsage;

typedef struct TextureSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} TextureSystemDescriptor;

typedef struct TbTexture TbTexture;

typedef struct TextureSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  TB_DYN_ARR_OF(TbTexture) textures;

  TbTextureId default_color_tex;
  TbTextureId default_normal_tex;
  TbTextureId default_metal_rough_tex;
  TbTextureId brdf_tex;
} TextureSystem;

void tb_texture_system_descriptor(SystemDescriptor *desc,
                                  const TextureSystemDescriptor *tex_desc);

VkImageView tb_tex_system_get_image_view(TextureSystem *self, TbTextureId tex);

TbTextureId tb_tex_system_create_texture(TextureSystem *self, const char *path,
                                         const char *name, TbTextureUsage usage,
                                         uint32_t width, uint32_t height,
                                         const uint8_t *pixels, uint64_t size);
TbTextureId tb_tex_system_load_texture(TextureSystem *self, const char *path,
                                       const char *name,
                                       const cgltf_texture *texture);
bool tb_tex_system_take_tex_ref(TextureSystem *self, TbTextureId id);
void tb_tex_system_release_texture_ref(TextureSystem *self, TbTextureId tex);
