#pragma once

#include "allocator.h"

#define RenderSystemId 0xABADBABE

typedef struct SystemDescriptor SystemDescriptor;

typedef struct RenderThread RenderThread;

typedef struct RenderSystemDescriptor {
  RenderThread *render_thread;
} RenderSystemDescriptor;

typedef struct RenderSystem {
  RenderThread *render_thread;
} RenderSystem;

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc);
