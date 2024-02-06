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

#include "infomode.h"
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

#include <SDL3/SDL_main.h>
int32_t main(int32_t argc, char *argv[]) {
  (void)argc;
  (void)argv;

  {
    const char *app_info = "Debug";
    size_t app_info_len = strlen(app_info);
    TracyCAppInfo(app_info, app_info_len)(void) app_info_len;

    TracyCSetThreadName("Main Thread");
  }

  // Create Temporary Arena Allocator
  TbArenaAllocator arena = {0};
  {
    const size_t arena_alloc_size = 1024ull * 1024ull * 512ull; // 512 MB
    tb_create_arena_alloc("Main Arena", &arena, arena_alloc_size);
  }

  TbGeneralAllocator gp_alloc = {0};
  tb_create_gen_alloc(&gp_alloc, "gp_alloc");

  TbAllocator alloc = gp_alloc.alloc;
  TbAllocator tmp_alloc = arena.alloc;

  {
    // This hint must be set before init for xbox controllers to work
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    int32_t res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMEPAD |
                           SDL_INIT_HAPTIC);
    if (res != 0) {
      SDL_TriggerBreakpoint();
      return -1;
    }
  }

  const char *app_name = "Toybox Sample";

  SDL_Window *window = SDL_CreateWindow(
      app_name, 1920, 1080, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (window == NULL) {
    SDL_Quit();
    SDL_TriggerBreakpoint();
    return -1;
  }

  TbWorldDesc world_desc = {
      .name = app_name,
      .argc = argc,
      .argv = argv,
      .gp_alloc = alloc,
      .tmp_alloc = tmp_alloc,
      .window = window,
  };
  TbWorld world = tb_create_world(&world_desc);

  tb_sample_on_start(&world);

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
      running = false; // NOLINT
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

  // This will also close the window that was provded
  tb_destroy_world(&world);

  SDL_Quit();

  tb_destroy_arena_alloc(arena);
  tb_destroy_gen_alloc(gp_alloc);

  return 0;
}
