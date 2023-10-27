#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "tbvk.h"
#include <SDL2/SDL_stdinc.h>

#define RenderTargetSystemId 0xB0BABABE

#define TB_SHADOW_MAP_DIM 4096

typedef uint32_t TbRenderTargetId;
static const TbRenderTargetId InvalidRenderTargetId = SDL_MAX_UINT32;

typedef struct RenderSystem RenderSystem;
typedef struct RenderTarget RenderTarget;

typedef struct ecs_world_t ecs_world_t;

typedef struct RenderTargetDescriptor {
  const char *name;
  VkFormat format;
  VkExtent3D extent;
  uint32_t mip_count;
  uint32_t layer_count;
  VkImageViewType view_type;
} RenderTargetDescriptor;

typedef struct RenderTargetSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  TB_DYN_ARR_OF(RenderTarget) render_targets;

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
} RenderTargetSystem;

void tb_register_render_target_sys(ecs_world_t *ecs, Allocator std_alloc,
                                   Allocator tmp_alloc);
void tb_unregister_render_target_sys(ecs_world_t *ecs);

void tb_reimport_swapchain(RenderTargetSystem *self);

TbRenderTargetId tb_import_render_target(RenderTargetSystem *self,
                                         const RenderTargetDescriptor *rt_desc,
                                         const VkImage *images);

TbRenderTargetId tb_create_render_target(RenderTargetSystem *self,
                                         const RenderTargetDescriptor *rt_desc);

uint32_t tb_render_target_get_mip_count(RenderTargetSystem *self,
                                        TbRenderTargetId rt);

VkExtent3D tb_render_target_get_extent(RenderTargetSystem *self,
                                       TbRenderTargetId rt);
VkExtent3D tb_render_target_get_mip_extent(RenderTargetSystem *self,
                                           uint32_t mip, TbRenderTargetId rt);

VkFormat tb_render_target_get_format(RenderTargetSystem *self,
                                     TbRenderTargetId rt);
VkImageView tb_render_target_get_view(RenderTargetSystem *self,
                                      uint32_t frame_idx, TbRenderTargetId rt);
VkImageView tb_render_target_get_mip_view(RenderTargetSystem *self,
                                          uint32_t mip, uint32_t frame_idx,
                                          TbRenderTargetId rt);
VkImage tb_render_target_get_image(RenderTargetSystem *self, uint32_t frame_idx,
                                   TbRenderTargetId rt);
