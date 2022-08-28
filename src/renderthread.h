#pragma once

#include "allocator.h"

#include "tbvk.h"

#if !defined(FINAL) && !defined(__ANDROID__)
#define VALIDATION
#endif

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_semaphore SDL_semaphore;
typedef struct SDL_mutex SDL_mutex;

typedef struct mi_heap_s mi_heap_t;

typedef struct RenderThreadDescriptor {
  SDL_Window *window;
} RenderThreadDescriptor;

#define MAX_FRAME_STATES 3

typedef struct FrameState {
  SDL_semaphore *wait_sem;
  SDL_semaphore *signal_sem;
} FrameState;

typedef struct RenderThread {
  uint32_t frame_idx;
  uint64_t frame_count;
  FrameState frame_states[MAX_FRAME_STATES];

  SDL_Window *window;
  SDL_Thread *thread;

  ArenaAllocator render_arena;

  mi_heap_t *vk_heap;
  VkAllocationCallbacks vk_alloc;

  VkInstance instance;
#ifdef VALIDATION
  VkDebugUtilsMessengerEXT debug_utils_messenger;
#endif
} RenderThread;

bool tb_start_render_thread(RenderThreadDescriptor *desc, RenderThread *thread);

void tb_signal_render(RenderThread *thread, uint32_t frame_idx);

void tb_wait_render(RenderThread *thread, uint32_t frame_idx);

void tb_stop_render_thread(RenderThread *thread);
