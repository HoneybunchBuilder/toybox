#pragma once

#include "allocator.h"

#define RenderTargetSystemId 0xB0BABABE

typedef uint64_t TbRenderTargetId;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct RenderTarget RenderTarget;

typedef struct RenderTargetSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderTargetSystemDescriptor;

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
