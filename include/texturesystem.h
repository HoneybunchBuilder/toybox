#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "dynarray.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

typedef struct TbWorld TbWorld;
typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbTexture TbTexture;
typedef struct VkImageView_T *VkImageView;
typedef struct cgltf_texture cgltf_texture;
typedef uint64_t TbTextureId;

static const TbTextureId TbInvalidTextureId = SDL_MAX_UINT64;

typedef enum TbTextureUsage {
  TB_TEX_USAGE_UNKNOWN = 0,
  TB_TEX_USAGE_COLOR,
  TB_TEX_USAGE_NORMAL,
  TB_TEX_USAGE_METAL_ROUGH,
  TB_TEX_USAGE_BRDF,
} TbTextureUsage;

typedef struct TbTextureSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;

  TB_DYN_ARR_OF(TbTexture) textures;

  TbTextureId default_color_tex;
  TbTextureId default_normal_tex;
  TbTextureId default_metal_rough_tex;
  TbTextureId brdf_tex;
} TbTextureSystem;

void tb_register_texture_sys(TbWorld *world);
void tb_unregister_texture_sys(TbWorld *world);

VkImageView tb_tex_system_get_image_view(TbTextureSystem *self,
                                         TbTextureId tex);
TbTextureId tb_tex_system_create_texture(TbTextureSystem *self,
                                         const char *path, const char *name,
                                         TbTextureUsage usage, uint32_t width,
                                         uint32_t height, const uint8_t *pixels,
                                         uint64_t size);
TbTextureId tb_tex_system_load_texture(TbTextureSystem *self, const char *path,
                                       const char *name,
                                       const cgltf_texture *texture);
bool tb_tex_system_take_tex_ref(TbTextureSystem *self, TbTextureId id);
void tb_tex_system_release_texture_ref(TbTextureSystem *self, TbTextureId tex);
