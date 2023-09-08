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
  ArenaAllocator arena = {0};
  {
    SDL_Log("%s", "Creating Arena Allocator");
    const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
    create_arena_allocator("Main Arena", &arena, arena_alloc_size);
  }

  StandardAllocator gp_alloc = {0};
  {
    SDL_Log("%s", "Creating Standard Allocator");
    create_standard_allocator(&gp_alloc, "std_alloc");
  }

  Allocator std_alloc = gp_alloc.alloc;
  Allocator tmp_alloc = arena.alloc;

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

#define NEW_TICK

#ifndef NEW_TICK
// Order does not matter
#define COMP_COUNT 8
  ComponentDescriptor component_descs[COMP_COUNT] = {0};
  {
    int32_t i = 0;
    tb_transform_component_descriptor(&component_descs[i++]);
    tb_camera_component_descriptor(&component_descs[i++]);
    tb_directional_light_component_descriptor(&component_descs[i++]);
    tb_noclip_component_descriptor(&component_descs[i++]);
    tb_sky_component_descriptor(&component_descs[i++]);
    tb_mesh_component_descriptor(&component_descs[i++]);
    tb_ocean_component_descriptor(&component_descs[i++]);
    tb_rotator_component_descriptor(&component_descs[i++]);

    TB_CHECK(i == COMP_COUNT, "Unexpected # of component descriptors");
  }

  InputSystemDescriptor input_system_desc = {
      .tmp_alloc = arena.alloc,
      .window = window,
  };

  NoClipControllerSystemDescriptor noclip_system_desc = {
      .tmp_alloc = arena.alloc,
  };

  CoreUISystemDescriptor coreui_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  ImGuiSystemDescriptor imgui_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
      .context_count = 1,
      .context_atlases[0] = NULL,
  };

  SkySystemDescriptor sky_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  MeshSystemDescriptor mesh_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  OceanSystemDescriptor ocean_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  MaterialSystemDescriptor material_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  TextureSystemDescriptor texture_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  ViewSystemDescriptor view_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderObjectSystemDescriptor render_object_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderSystemDescriptor render_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
      .render_thread = render_thread,
  };

  CameraSystemDescriptor camera_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderTargetSystemDescriptor render_target_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderPipelineSystemDescriptor render_pipeline_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  ShadowSystemDescriptor shadow_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  TimeOfDaySystemDescriptor tod_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  VisualLoggingSystemDescriptor vlog_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RotatorSystemDescriptor rotator_system_desc = {
      .tmp_alloc = arena.alloc,
  };

  AudioSystemDescriptor audio_system_desc = {
      .std_alloc = gp_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

// Order doesn't matter here
#define SYSTEM_COUNT 20
  SystemDescriptor system_descs[SYSTEM_COUNT] = {0};
  {
    uint32_t i = 0;
    tb_input_system_descriptor(&system_descs[i++], &input_system_desc);
    tb_noclip_controller_system_descriptor(&system_descs[i++],
                                           &noclip_system_desc);
    tb_coreui_system_descriptor(&system_descs[i++], &coreui_system_desc);
    tb_imgui_system_descriptor(&system_descs[i++], &imgui_system_desc);
    tb_sky_system_descriptor(&system_descs[i++], &sky_system_desc);
    tb_ocean_system_descriptor(&system_descs[i++], &ocean_system_desc);
    tb_mesh_system_descriptor(&system_descs[i++], &mesh_system_desc);
    tb_material_system_descriptor(&system_descs[i++], &material_system_desc);
    tb_texture_system_descriptor(&system_descs[i++], &texture_system_desc);
    tb_render_object_system_descriptor(&system_descs[i++],
                                       &render_object_system_desc);
    tb_view_system_descriptor(&system_descs[i++], &view_system_desc);
    tb_render_system_descriptor(&system_descs[i++], &render_system_desc);
    tb_camera_system_descriptor(&system_descs[i++], &camera_system_desc);
    tb_render_target_system_descriptor(&system_descs[i++],
                                       &render_target_system_desc);
    tb_render_pipeline_system_descriptor(&system_descs[i++],
                                         &render_pipeline_system_desc);
    tb_shadow_system_descriptor(&system_descs[i++], &shadow_system_desc);
    tb_time_of_day_system_descriptor(&system_descs[i++], &tod_system_desc);
    tb_visual_logging_system_descriptor(&system_descs[i++], &vlog_system_desc);
    tb_rotator_system_descriptor(&system_descs[i++], &rotator_system_desc);
    tb_audio_system_descriptor(&system_descs[i++], &audio_system_desc);
    TB_CHECK(i == SYSTEM_COUNT, "Incorrect number of systems");
  }

  // But it does matter here
  SystemId init_order[SYSTEM_COUNT];
  {
    uint32_t i = 0;
    init_order[i++] = AudioSystemId;
    init_order[i++] = RenderSystemId;
    init_order[i++] = InputSystemId;
    init_order[i++] = RenderTargetSystemId;
    init_order[i++] = TextureSystemId;
    init_order[i++] = ViewSystemId;
    init_order[i++] = RenderObjectSystemId;
    init_order[i++] = RenderPipelineSystemId;
    init_order[i++] = MaterialSystemId;
    init_order[i++] = MeshSystemId;
    init_order[i++] = SkySystemId;
    init_order[i++] = ImGuiSystemId;
    init_order[i++] = NoClipControllerSystemId;
    init_order[i++] = CoreUISystemId;
    init_order[i++] = VisualLoggingSystemId;
    init_order[i++] = OceanSystemId;
    init_order[i++] = CameraSystemId;
    init_order[i++] = ShadowSystemId;
    init_order[i++] = TimeOfDaySystemId;
    init_order[i++] = RotatorSystemId;
    TB_CHECK(i == SYSTEM_COUNT, "Incorrect number of systems");
  }

  WorldDescriptor world_desc = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .component_count = COMP_COUNT,
      .component_descs = component_descs,
      .system_count = SYSTEM_COUNT,
      .system_descs = system_descs,
      .init_order = init_order,
  };

  World world = {0};
  bool success = tb_create_world(&world_desc, &world);
  TB_CHECK_RETURN(success, "Failed to create world.", -1);

  // Load sample scene
  tb_sample_on_start(&world);

#undef COMP_COUNT
#undef SYSTEM_COUNT

#else
  // Register Gen 2 Systems and Components
  ecs_world_t *ecs_world =
      tb_create_world2(std_alloc, tmp_alloc, render_thread, window);
#endif

  // Main loop
  bool running = true;

  uint64_t time = 0;
  uint64_t start_time = SDL_GetPerformanceCounter();
  uint64_t last_time = 0;
  uint64_t delta_time = 0;
  float delta_time_seconds = 0.0f;

  while (running) {
    TracyCFrameMarkStart("Simulation Frame");
    TracyCZoneN(trcy_ctx, "Simulation Frame", true);
    TracyCZoneColor(trcy_ctx, TracyCategoryColorCore);

    // Use SDL High Performance Counter to get timing info
    time = SDL_GetPerformanceCounter() - start_time;
    delta_time = time - last_time;
    delta_time_seconds =
        (float)((double)delta_time / (double)(SDL_GetPerformanceFrequency()));
    last_time = time;

#ifndef NEW_TICK
    // Tick the world
    if (!tb_tick_world(&world, delta_time_seconds)) {
      running = false;
      TracyCZoneEnd(trcy_ctx);
      TracyCFrameMarkEnd("Simulation Frame");
      break;
    }
#else
    if (!tb_tick_world2(ecs_world, delta_time_seconds)) {
      running = false;
      TracyCZoneEnd(trcy_ctx);
      TracyCFrameMarkEnd("Simulation Frame");
      break;
    }
#endif

    // Reset the arena allocator
    arena = reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx);
    TracyCFrameMarkEnd("Simulation Frame");
  }

  // Stop the render thread before we start destroying render objects
  tb_stop_render_thread(render_thread);

#ifndef NEW_TICK
  tb_destroy_world(&world);
#else
  tb_destroy_world2(ecs_world);
#endif

  // Destroying the render thread will also close the window
  tb_destroy_render_thread(render_thread);
  tb_free(gp_alloc.alloc, render_thread);
  render_thread = NULL;
  window = NULL;

  SDL_Quit();

  destroy_arena_allocator(arena);
  destroy_standard_allocator(gp_alloc);

  return 0;
}
