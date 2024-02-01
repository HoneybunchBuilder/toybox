#include <mimalloc.h>

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
#include "viewersystem.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"

#include "renderthread.h"

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
  tb_create_gen_alloc(&gp_alloc, "std_alloc");

  TbAllocator std_alloc = gp_alloc.alloc;
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

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
  }

  SDL_Window *window = SDL_CreateWindow(
      "Toybox Viewer", 1920, 1080, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (window == NULL) {
    SDL_Quit();
    SDL_TriggerBreakpoint();
    return -1;
  }

  // Must create render thread on the heap like this
  TbRenderThread *render_thread = tb_alloc_tp(std_alloc, TbRenderThread);
  TbRenderThreadDescriptor render_thread_desc = {
      .window = window,
  };
  TB_CHECK(tb_start_render_thread(&render_thread_desc, render_thread),
           "Failed to start render thread");

  // Do not go initializing anything until we know the render thread is ready
  tb_wait_thread_initialized(render_thread);

  // Create the world with the additional viewer system
  tb_auto create_world =
      ^(TbWorld *world, TbRenderThread *thread, SDL_Window *window) {
        tb_create_default_world(world, thread, window);
        tb_register_viewer_sys(world);
      };
  TbWorld world = tb_create_world(std_alloc, tmp_alloc, create_world,
                                  render_thread, window);

  // Main loop
  bool running = true;

  uint64_t time = 0;
  uint64_t start_time = SDL_GetPerformanceCounter();
  uint64_t last_time = 0;
  uint64_t delta_time = 0;
  float delta_time_seconds = 0.0F;

  while (running) {
    TracyCFrameMarkStart("Simulation Frame");
    TracyCZoneN(trcy_ctx, "Simulation Frame", true);
    TracyCZoneColor(trcy_ctx, TracyCategoryColorCore);

    ECS_COMPONENT(world.ecs, TbViewerSystem);

    // Before we tick the world go check the ViewerSystem and see if the user
    // requested that we change scene. In which case we perform a load before
    // ticking
    TbViewerSystem *viewer = ecs_singleton_get_mut(world.ecs, TbViewerSystem);
    if (viewer) {
      // Order matters; we can get both signals at once
      if (viewer->unload_scene_signal) {
        // TODO: Properly wait for the render thread to be finished otherwise
        // we'll destroy resources in flight
        tb_unload_scene(&world, &world.scenes.data[0]);
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
      TracyCZoneEnd(trcy_ctx);
      TracyCFrameMarkEnd("Simulation Frame");
      break;
    }

    // Reset the arena allocator
    arena = tb_reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx);
    TracyCFrameMarkEnd("Simulation Frame");
  }
  return 0;

  tb_clear_world(&world);

  // Stop the render thread before we start destroying render objects
  tb_stop_render_thread(render_thread);

  // Unregister app specific systems
  tb_unregister_viewer_sys(&world);

  tb_destroy_world(&world);

  // Destroying the render thread will also close the window
  tb_destroy_render_thread(render_thread);
  tb_free(std_alloc, render_thread);
  render_thread = NULL;
  window = NULL;

  SDL_Quit();

  tb_destroy_arena_alloc(arena);
  tb_destroy_gen_alloc(gp_alloc);

  return 0;
}
