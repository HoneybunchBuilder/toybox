#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "tbvk.h"
#include <SDL3/SDL_stdinc.h>

#define TB_SHADOW_MAP_DIM 4096

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbRenderTarget TbRenderTarget;
typedef struct TbWorld TbWorld;
typedef uint32_t TbRenderTargetId;

static const TbRenderTargetId TbInvalidRenderTargetId = SDL_MAX_UINT32;

typedef struct TbRenderTargetDescriptor {
  const char *name;
  VkFormat format;
  VkExtent3D extent;
  uint32_t mip_count;
  uint32_t layer_count;
  VkImageViewType view_type;
} TbRenderTargetDescriptor;

typedef struct TbRenderTargetSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;

  TB_DYN_ARR_OF(TbRenderTarget) render_targets;

  TbRenderTargetId swapchain;
  TbRenderTargetId depth_buffer;
  TbRenderTargetId normal_buffer;
  TbRenderTargetId hdr_color;
  TbRenderTargetId depth_buffer_copy;
  TbRenderTargetId color_copy;
  TbRenderTargetId env_cube;
  TbRenderTargetId irradiance_map;
  TbRenderTargetId prefiltered_cube;
  TbRenderTargetId shadow_map;
  TbRenderTargetId brightness;
  TbRenderTargetId bloom_mip_chain;
  TbRenderTargetId ldr_target;
} TbRenderTargetSystem;

void tb_register_render_target_sys(TbWorld *world);
void tb_unregister_render_target_sys(TbWorld *world);

void tb_reimport_swapchain(TbRenderTargetSystem *self);

TbRenderTargetId
tb_import_render_target(TbRenderTargetSystem *self,
                        const TbRenderTargetDescriptor *rt_desc,
                        const VkImage *images);

TbRenderTargetId
tb_create_render_target(TbRenderTargetSystem *self,
                        const TbRenderTargetDescriptor *rt_desc);

uint32_t tb_render_target_get_mip_count(TbRenderTargetSystem *self,
                                        TbRenderTargetId rt);

VkExtent3D tb_render_target_get_extent(TbRenderTargetSystem *self,
                                       TbRenderTargetId rt);
VkExtent3D tb_render_target_get_mip_extent(TbRenderTargetSystem *self,
                                           uint32_t layer, uint32_t mip,
                                           TbRenderTargetId rt);

VkFormat tb_render_target_get_format(TbRenderTargetSystem *self,
                                     TbRenderTargetId rt);
VkImageView tb_render_target_get_view(TbRenderTargetSystem *self,
                                      uint32_t frame_idx, TbRenderTargetId rt);
VkImageView tb_render_target_get_mip_view(TbRenderTargetSystem *self,
                                          uint32_t layer, uint32_t mip,
                                          uint32_t frame_idx,
                                          TbRenderTargetId rt);
VkImage tb_render_target_get_image(TbRenderTargetSystem *self,
                                   uint32_t frame_idx, TbRenderTargetId rt);
