#include <mimalloc.h>

#include "tb_common.h"
#include "tb_engine_config.h"
#include "tb_profiling.h"
#include "tb_sdl.h"
#include "tb_settings.h"
#include "tb_simd.h"
#include "tb_vk.h"
#include "tb_vma.h"
#include "tb_world.h"

#include "viewersystem.h"

#include <flecs.h>

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
    const size_t arena_alloc_size = 1024ULL * 1024ULL * 512ULL; // 512 MB
    tb_create_arena_alloc("Main Arena", &arena, arena_alloc_size);
  }

  TbGeneralAllocator gp_alloc = {0};
  tb_create_gen_alloc(&gp_alloc, "gp_alloc");

  TbAllocator std_alloc = gp_alloc.alloc;
  TbAllocator tmp_alloc = arena.alloc;

  {
    // This hint must be set before init for xbox controllers to work
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    int32_t res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC);
    if (res != 0) {
      SDL_TriggerBreakpoint();
      return -1;
    }

    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
  }

  const char *app_name = "Toybox Viewer";

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
      .gp_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .window = window,
  };
  TbWorld world = {0};
  bool ok = tb_create_world(&world_desc, &world);
  if (!ok) {
    return 1;
  }

  // Main loop
  bool running = true;

  uint64_t time = 0;
  uint64_t start_time = SDL_GetPerformanceCounter();
  uint64_t last_time = 0;
  uint64_t delta_time = 0;
  float delta_time_seconds = 0.0F;

  while (running) {
    TracyCFrameMarkStart("Simulation Frame");
    TB_TRACY_SCOPEC("Simulation Frame", TracyCategoryColorCore);

    // Before we tick the world go check the ViewerSystem and see if the user
    // requested that we change scene. In which case we perform a load before
    // ticking
    TbViewerSystem *viewer = ecs_singleton_ensure(world.ecs, TbViewerSystem);
    if (viewer) {
      // Order matters; we can get both signals at once
      if (viewer->unload_scene_signal) {
        // TODO: Properly wait for the render thread to be finished otherwise
        // we'll destroy resources in flight
        // tb_unload_scene(&world, &world.scenes.data[0]);
        viewer->unload_scene_signal = false;
      }
      if (viewer->load_scene_signal) {
        tb_load_scene(&world, viewer->selected_scene);
        viewer->load_scene_signal = false;
      }
    }

    // Use SDL High Performance Counter to get timing info
    time = SDL_GetPerformanceCounter() - start_time;
    delta_time = time - last_time;
    delta_time_seconds =
        (float)((double)delta_time / (double)(SDL_GetPerformanceFrequency()));
    last_time = time;

    // Tick the world
    if (!tb_tick_world(&world, delta_time_seconds)) {
      running = false; // NOLINT
      TracyCFrameMarkEnd("Simulation Frame");
      break;
    }

    // Reset the arena allocator
    arena = tb_reset_arena(arena, true); // Just allow it to grow for now

    TracyCFrameMarkEnd("Simulation Frame");
  }
  return 0;

  tb_destroy_world(&world);

  SDL_Quit();

  tb_destroy_arena_alloc(arena);
  tb_destroy_gen_alloc(gp_alloc);

  return 0;
}
