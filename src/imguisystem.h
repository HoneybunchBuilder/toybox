#pragma once

#include "tbcommon.h"
#include "world.h"

#define ImGuiSystemId 0xDEADFA11

typedef struct RenderThread RenderThread;

typedef struct ImGuiSystemDescriptor {
  RenderThread *render_thread;
  Allocator tmp_alloc;
} ImGuiSystemDescriptor;

typedef struct ImGuiSystem {
  RenderThread *render_thread;
  Allocator tmp_alloc;
} ImGuiSystem;

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *imgui_desc);
