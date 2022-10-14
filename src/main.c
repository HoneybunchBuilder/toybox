#include <mimalloc.h>

#include "allocator.h"
#include "assetmanifest.h"
#include "camera.h"
#include "config.h"
#include "demo.h"
#include "pi.h"
#include "profiling.h"
#include "settings.h"
#include "shadercommon.h"
#include "simd.h"
#include "world.h"

#include "tbcommon.h"
#include "tbsdl.h"
#include "tbvk.h"
#include "tbvma.h"

#include "cameracomponent.h"
#include "coreuicomponent.h"
#include "imguicomponent.h"
#include "inputcomponent.h"
#include "lightcomponent.h"
#include "meshcomponent.h"
#include "noclipcomponent.h"
#include "oceancomponent.h"
#include "skycomponent.h"
#include "transformcomponent.h"

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
#include "skysystem.h"
#include "texturesystem.h"
#include "viewsystem.h"

#include "renderthread.h"

int32_t SDL_main(int32_t argc, char *argv[]) {
  (void)argc;
  (void)argv;
  SDL_Log("%s", "Entered SDL_main");
  {
    const char *app_info = TB_APP_INFO_STR;
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

  StandardAllocator std_alloc = {0};
  {
    SDL_Log("%s", "Creating Standard Allocator");
    create_standard_allocator(&std_alloc, "std_alloc");
  }

  {
    int32_t res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    if (res != 0) {
      const char *msg = SDL_GetError();
      SDL_Log("Failed to initialize SDL with error: %s", msg);
      SDL_TriggerBreakpoint();
      return -1;
    }

    int32_t flags = IMG_INIT_PNG;
    res = IMG_Init(flags);
    if ((res & IMG_INIT_PNG) == 0) {
      const char *msg = IMG_GetError();
      SDL_Log("Failed to initialize SDL_Image with error: %s", msg);
      SDL_TriggerBreakpoint();
      return -1;
    }

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
  }

  SDL_Window *window =
      SDL_CreateWindow("Toybox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                       1920, 1080, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (window == NULL) {
    const char *msg = SDL_GetError();
    SDL_Log("Failed to open window with error: %s", msg);
    SDL_Quit();
    SDL_TriggerBreakpoint();
    return -1;
  }

  // Must create render thread on the heap like this
  RenderThread *render_thread = tb_alloc_tp(std_alloc.alloc, RenderThread);
  RenderThreadDescriptor render_thread_desc = {
      .window = window,
  };
  TB_CHECK(tb_start_render_thread(&render_thread_desc, render_thread),
           "Failed to start render thread");

  // Order does not matter
  const uint32_t component_count = 10;
  ComponentDescriptor component_descs[component_count] = {0};
  tb_transform_component_descriptor(&component_descs[0]);
  tb_camera_component_descriptor(&component_descs[1]);
  tb_directional_light_component_descriptor(&component_descs[2]);
  tb_noclip_component_descriptor(&component_descs[3]);
  tb_input_component_descriptor(&component_descs[4]);
  tb_coreui_component_descriptor(&component_descs[5]);
  tb_imgui_component_descriptor(&component_descs[6]);
  tb_sky_component_descriptor(&component_descs[7]);
  tb_mesh_component_descriptor(&component_descs[8]);
  tb_ocean_component_descriptor(&component_descs[9]);

  InputSystemDescriptor input_system_desc = {
      .tmp_alloc = arena.alloc,
      .window = window,
  };

  NoClipControllerSystemDescriptor noclip_system_desc = {
      .tmp_alloc = arena.alloc,
  };

  CoreUISystemDescriptor coreui_system_desc = {
      .tmp_alloc = arena.alloc,
  };

  ImGuiSystemDescriptor imgui_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  SkySystemDescriptor sky_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  MeshSystemDescriptor mesh_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  OceanSystemDescriptor ocean_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  MaterialSystemDescriptor material_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  TextureSystemDescriptor texture_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  ViewSystemDescriptor view_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderObjectSystemDescriptor render_object_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderSystemDescriptor render_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
      .render_thread = render_thread,
  };

  CameraSystemDescriptor camera_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderTargetSystemDescriptor render_target_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  RenderPipelineSystemDescriptor render_pipeline_system_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
  };

  // Order doesn't matter here
  const uint32_t system_count = 15;
  SystemDescriptor system_descs[system_count] = {0};
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
    TB_CHECK(i == system_count, "Incorrect number of systems");
  }

  // But it does matter here
  SystemId init_order[system_count];
  {
    uint32_t i = 0;
    init_order[i++] = RenderSystemId;
    init_order[i++] = InputSystemId;
    init_order[i++] = RenderTargetSystemId;
    init_order[i++] = ViewSystemId;
    init_order[i++] = RenderObjectSystemId;
    init_order[i++] = TextureSystemId;
    init_order[i++] = RenderPipelineSystemId;
    init_order[i++] = MaterialSystemId;
    init_order[i++] = MeshSystemId;
    init_order[i++] = SkySystemId;
    init_order[i++] = OceanSystemId;
    init_order[i++] = CameraSystemId;
    init_order[i++] = ImGuiSystemId;
    init_order[i++] = NoClipControllerSystemId;
    init_order[i++] = CoreUISystemId;
    TB_CHECK(i == system_count, "Incorrect number of systems");
  }
  SystemId tick_order[system_count];
  {
    uint32_t i = 0;
    tick_order[i++] = RenderPipelineSystemId;
    tick_order[i++] = InputSystemId;
    tick_order[i++] = NoClipControllerSystemId;
    tick_order[i++] = CoreUISystemId;
    tick_order[i++] = CameraSystemId;
    tick_order[i++] = ViewSystemId;
    tick_order[i++] = RenderObjectSystemId;
    tick_order[i++] = TextureSystemId;
    tick_order[i++] = MaterialSystemId;
    tick_order[i++] = MeshSystemId;
    tick_order[i++] = OceanSystemId;
    tick_order[i++] = SkySystemId;
    tick_order[i++] = ImGuiSystemId;
    tick_order[i++] = RenderTargetSystemId;
    tick_order[i++] = RenderSystemId;
    TB_CHECK(i == system_count, "Incorrect number of systems");
  }

  WorldDescriptor world_desc = {
      .std_alloc = std_alloc.alloc,
      .tmp_alloc = arena.alloc,
      .component_count = component_count,
      .component_descs = component_descs,
      .system_count = system_count,
      .system_descs = system_descs,
      .init_order = init_order,
      .tick_order = tick_order,
  };

  // Do not go initializing anything until we know the render thread is ready
  tb_wait_thread_initialized(render_thread);

  World world = {0};
  bool success = tb_create_world(&world_desc, &world);
  TB_CHECK_RETURN(success, "Failed to create world.", -1);

  // Create entity with some default components
  ImGuiComponentDescriptor imgui_comp_desc = {
      .font_atlas = NULL,
  };
  const uint32_t core_comp_count = 3;
  ComponentId core_comp_ids[3] = {InputComponentId, CoreUIComponentId,
                                  ImGuiComponentId};
  InternalDescriptor core_comp_descs[3] = {
      NULL,
      NULL,
      &imgui_comp_desc,
  };
  EntityDescriptor entity_desc = {
      .name = "Core",
      .component_count = core_comp_count,
      .component_ids = core_comp_ids,
      .component_descriptors = core_comp_descs,
  };
  tb_world_add_entity(&world, &entity_desc);

  // Get scene asset paths
  const char **scene_asset_paths =
      tb_alloc_nm_tp(arena.alloc, tb_scene_database_num, const char *);
  for (uint32_t i = 0; i < tb_scene_database_num; ++i) {
    const uint32_t scene_idx = tb_scene_database[i];
    SDL_assert(scene_idx < tb_asset_database_num);
    scene_asset_paths[i] = tb_asset_database[scene_idx];
  }

  // Load starter scene into world
  int32_t scene_idx = 0;
  const char *scene_path = scene_asset_paths[scene_idx];
  success = tb_world_load_scene(&world, scene_path);
  TB_CHECK_RETURN(success, "Failed to load scene.", -1);

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

    // Tick the world
    if (!tb_tick_world(&world, delta_time_seconds)) {
      running = false;
      TracyCZoneEnd(trcy_ctx);
      TracyCFrameMarkEnd("Simulation Frame");
      break;
    }

    // Reset the arena allocator
    arena = reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx);
    TracyCFrameMarkEnd("Simulation Frame");
  }

  // Stop the render thread before we start destroying render objects
  tb_stop_render_thread(render_thread);

  tb_destroy_world(&world);

  // Destroying the render thread will also close the window
  tb_destroy_render_thread(render_thread);
  tb_free(std_alloc.alloc, render_thread);
  render_thread = NULL;
  window = NULL;

  IMG_Quit();
  SDL_Quit();

  destroy_arena_allocator(arena);
  destroy_standard_allocator(std_alloc);

  return 0;
}
