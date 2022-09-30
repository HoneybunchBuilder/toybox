#pragma once

#include "allocator.h"
#include "tbvk.h"
#include <SDL2/SDL_stdinc.h>

#define RenderTargetSystemId 0xB0BABABE

typedef uint64_t TbRenderTargetId;
static const TbRenderTargetId InvalidRenderTargetId = SDL_MAX_UINT64;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct RenderTarget RenderTarget;

typedef struct RenderTargetSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderTargetSystemDescriptor;

typedef struct RenderTargetDescriptor {
  VkFormat format;
  VkExtent3D extent;
} RenderTargetDescriptor;

typedef struct RenderTargetSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  uint32_t rt_count;
  RenderTarget *render_targets;
  uint32_t rt_max;

  TbRenderTargetId swapchain;
  TbRenderTargetId depth_buffer;
  TbRenderTargetId depth_buffer_copy;
} RenderTargetSystem;

void tb_render_target_system_descriptor(
    SystemDescriptor *desc, const RenderTargetSystemDescriptor *rt_desc);

TbRenderTargetId tb_create_render_target(RenderTargetSystem *self,
                                         const RenderTargetDescriptor *rt_desc);
