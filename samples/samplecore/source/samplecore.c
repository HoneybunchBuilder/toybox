#include "samplecore.h"

#include <mimalloc.h>

#include <flecs.h>

#include "allocator.h"
#include "pi.h"
#include "profiling.h"
#include "settings.h"
#include "shadercommon.h"
#include "simd.h"
#include "tbengineconfig.h"
#include "world.h"

#include "tbcommon.h"
#include "tbsdl.h"
#include "tbvk.h"
#include "tbvma.h"

#include "cameracomponent.h"
#include "lightcomponent.h"
#include "meshcomponent.h"
#include "noclipcomponent.h"
#include "oceancomponent.h"
#include "skycomponent.h"
#include "transformcomponent.h"
#include "transformercomponents.h"

#include "audiosystem.h"
#include "camerasystem.h"
#include "coreuisystem.h"
#include "imguisystem.h"
#include "inputsystem.h"
#include "materialsystem.h"
#include "meshsystem.h"
#include "noclipcontrollersystem.h"
#include "oceansystem.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "rotatorsystem.h"
#include "shadowsystem.h"
#include "skysystem.h"
#include "texturesystem.h"
#include "timeofdaysystem.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"

#include "renderthread.h"

int32_t SDL_main(int32_t argc, char *argv[]) {
  (void)argc;
  (void)argv;

  SDL_Log("%s", "Entered SDL_main");
  {
    const char *app_info = "Debug";
    size_t app_info_len = strlen(app_info);
    TracyCAppInfo(app_info, app_info_len)(void) app_info_len;

    TracyCSetThreadName("Main Thread");
  }

  // Create Temporary Arena Allocator
  TbArenaAllocator arena = {0};
  {
    SDL_Log("%s", "Creating Arena Allocator");
    const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
    tb_create_arena_alloc("Main Arena", &arena, arena_alloc_size);
  }

  TbGeneralAllocator gp_alloc = {0};
  {
    SDL_Log("%s", "Creating Standard Allocator");
    tb_create_gen_alloc(&gp_alloc, "std_alloc");
  }

  TbAllocator std_alloc = gp_alloc.alloc;
  TbAllocator tmp_alloc = arena.alloc;

  {
    int32_t res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER |
                           SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
    if (res != 0) {
      const char *msg = SDL_GetError();
      SDL_Log("Failed to initialize SDL with error: %s", msg);
      SDL_TriggerBreakpoint();
      return -1;
    }

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
  }

  SDL_Window *window = SDL_CreateWindow(
      "Toybox Sample", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920,
      1080, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (window == NULL) {
    const char *msg = SDL_GetError();
    SDL_Log("Failed to open window with error: %s", msg);
    SDL_Quit();
    SDL_TriggerBreakpoint();
    return -1;
  }

  // Must create render thread on the heap like this
  RenderThread *render_thread = tb_alloc_tp(gp_alloc.alloc, RenderThread);
  RenderThreadDescriptor render_thread_desc = {
      .window = window,
  };
  TB_CHECK(tb_start_render_thread(&render_thread_desc, render_thread),
           "Failed to start render thread");

  // Do not go initializing anything until we know the render thread is ready
  tb_wait_thread_initialized(render_thread);

  TbWorld world =
      tb_create_world(std_alloc, tmp_alloc, NULL, render_thread, window);
  tb_sample_on_start(&world);

  // Main loop
  bool running = true;

  uint64_t time = 0;
  uint64_t start_time = SDL_GetPerformanceCounter();
  uint64_t last_time = 0;
  uint64_t delta_time = 0;
  float delta_time_seconds = 0.0f;

  while (running) {
    TracyCZoneN(trcy_ctx, "Simulation Frame", true);
    TracyCZoneColor(trcy_ctx, TracyCategoryColorCore);

    // Use SDL High Performance Counter to get timing info
    time = SDL_GetPerformanceCounter() - start_time;
    delta_time = time - last_time;
    delta_time_seconds =
        (float)((double)delta_time / (double)(SDL_GetPerformanceFrequency()));
    last_time = time;

    if (!tb_tick_world(&world, delta_time_seconds)) {
      running = false;
      TracyCZoneEnd(trcy_ctx);
      break;
    }

    // Reset the arena allocator
    arena = tb_reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx);
  }
  return 0;

  // This doesn't quite work yet
  tb_clear_world(&world);

  // Stop the render thread before we start destroying render objects
  tb_stop_render_thread(render_thread);

  tb_destroy_world(&world);

  // Destroying the render thread will also close the window
  tb_destroy_render_thread(render_thread);
  tb_free(gp_alloc.alloc, render_thread);
  render_thread = NULL;
  window = NULL;

  SDL_Quit();

  tb_destroy_arena_alloc(arena);
  tb_destroy_gen_alloc(gp_alloc);

  return 0;
}
