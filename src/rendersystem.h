#pragma once

#include "allocator.h"

#define RenderSystemId 0xABADBABE

typedef struct SystemDescriptor SystemDescriptor;

typedef struct SDL_Window SDL_Window;
typedef struct VkInstance_T *VkInstance;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

typedef struct RenderSystemDescriptor {
  SDL_Window *window;
  VkInstance instance;
  Allocator std_alloc;
  Allocator tmp_alloc;
  const VkAllocationCallbacks *vk_alloc;
} RenderSystemDescriptor;

typedef struct RenderSystem {
  SDL_Window *window;
  VkInstance instance;
  Allocator std_alloc;
  Allocator tmp_alloc;
  const VkAllocationCallbacks *vk_alloc;
} RenderSystem;

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc);
