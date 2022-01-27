#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>

#ifdef __SWITCH__
#define mi_heap_t int
#define mi_heap_new() 0
#define mi_heap_delete(...)

#define mi_heap_malloc_aligned(heap, size, alignment) malloc(size)
#define mi_heap_realloc_aligned(heap, ptr, size, alignment) realloc(ptr, size)
#define mi_free(ptr) free(ptr)
#else
#include <mimalloc.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <volk.h>

#include "vk_mem_alloc.h"

#include "allocator.h"
#include "camera.h"
#include "config.h"

#include "demo.h"
#include "pi.h"
#include "profiling.h"
#include "settings.h"
#include "shadercommon.h"
#include "simd.h"

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifndef FINAL
#define VALIDATION
#endif

#define WIDTH 1600
#define HEIGHT 900

#ifdef VALIDATION
static bool check_layer(const char *check_name, uint32_t layer_count,
                        VkLayerProperties *layers) {
  bool found = false;
  for (uint32_t i = 0; i < layer_count; i++) {
    if (!strcmp(check_name, layers[i].layerName)) {
      found = true;
      break;
    }
  }
  return found;
}

#ifndef __ANDROID__
static VkBool32
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                  void *pUserData) {
  (void)messageTypes;
  (void)pUserData;

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity &
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else {
    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  }

  return false;
}
#endif
#endif

static void *vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                         VkSystemAllocationScope scope) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  (void)scope;
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  void *ptr = mi_heap_malloc_aligned(heap, size, alignment);
  TracyCAllocN(ptr, size, "Vulkan");
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                           size_t alignment, VkSystemAllocationScope scope) {
  (void)scope;
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  TracyCFreeN(pOriginal, "Vulkan");
  void *ptr = mi_heap_realloc_aligned(heap, pOriginal, size, alignment);
  TracyCAllocN(ptr, size, "Vulkan");
  TracyCZoneEnd(ctx);
  return ptr;
}

static void vk_free_fn(void *pUserData, void *pMemory) {
  (void)pUserData;
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(pMemory, "Vulkan");
  mi_free(pMemory);
  TracyCZoneEnd(ctx);
}

static VkAllocationCallbacks create_vulkan_allocator(mi_heap_t *heap) {
  VkAllocationCallbacks ret = {
      .pUserData = heap,
      .pfnAllocation = vk_alloc_fn,
      .pfnReallocation = vk_realloc_fn,
      .pfnFree = vk_free_fn,
  };
  return ret;
}

int32_t SDL_main(int32_t argc, char *argv[]) {
  (void)argc;
  (void)argv;
  SDL_Log("%s", "Entered SDL_main");
  static const float qtr_pi = 0.7853981625f;

  {
    const char *app_info = HB_APP_INFO_STR;
    size_t app_info_len = strlen(app_info);
    TracyCAppInfo(app_info, app_info_len);
    (void)app_info_len;
  }

  // Create Temporary Arena Allocator
  SDL_Log("%s", "Creating Arena Allocator");
  static const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
  ArenaAllocator arena = {0};
  create_arena_allocator(&arena, arena_alloc_size);

  mi_heap_t *vk_heap = mi_heap_new();
  SDL_Log("%s", "Creating Vulkan Allocator");
  VkAllocationCallbacks vk_alloc = create_vulkan_allocator(vk_heap);
  SDL_Log("%s", "Creating Standard Allocator");
  StandardAllocator std_alloc = {0};
  create_standard_allocator(&std_alloc, "std_alloc");

  const VkAllocationCallbacks *vk_alloc_ptr = &vk_alloc;

  if (!igDebugCheckVersionAndDataLayout(
          igGetVersion(), sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2),
          sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx))) {
    SDL_Log("%s", "Failed to validate imgui data");
    SDL_TriggerBreakpoint();
    return -1;
  }

  // Setup settings
  // TODO: Save and load to disk
  TBSettings settings = {
      .display_mode = {WIDTH, HEIGHT, 60.0f},
      .resolution_scale = 1.0f,
  };

  EditorCameraController controller = {0};
  controller.move_speed = 10.0f;
  controller.look_speed = 1.0f;

  Camera main_cam = {
      .transform =
          {
              .position = {0, -1, 10},
              .scale = {1, 1, 1},
          },
      .aspect = (float)WIDTH / (float)HEIGHT,
      .fov = qtr_pi * 2,
      .near = 0.01f,
      .far = 100.0f,
  };

  SDL_Log("%s", "Initializing Volk");
  VkResult err = volkInitialize();
  if (err != VK_SUCCESS) {
    SDL_Log("%s", "Failed to initialize volk");
    SDL_TriggerBreakpoint();
    return (int32_t)err;
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
                       WIDTH, HEIGHT, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (window == NULL) {
    const char *msg = SDL_GetError();
    SDL_Log("Failed to open window with error: %s", msg);
    SDL_Quit();
    SDL_TriggerBreakpoint();
    return -1;
  }

  // Create vulkan instance
  VkInstance instance = VK_NULL_HANDLE;
  {
    // Gather required layers
    uint32_t layer_count = 0;
    const char *layer_names[MAX_LAYER_COUNT] = {0};

    {
      uint32_t instance_layer_count = 0;
      err = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
      assert(err == VK_SUCCESS);
      if (instance_layer_count > 0) {
        VkLayerProperties *instance_layers = hb_alloc_nm_tp(
            arena.alloc, instance_layer_count, VkLayerProperties);
        err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                                                 instance_layers);
        assert(err == VK_SUCCESS);
#ifdef VALIDATION
        {
          const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

          bool validation_found = check_layer(
              validation_layer_name, instance_layer_count, instance_layers);
          if (validation_found) {
            assert(layer_count + 1 < MAX_LAYER_COUNT);
            layer_names[layer_count++] = validation_layer_name;
          }
        }
#endif
      }
    }

    // Query SDL for required extensions
    uint32_t ext_count = 0;
    const char *ext_names[MAX_EXT_COUNT] = {0};
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, NULL);

    assert(ext_count < MAX_EXT_COUNT);
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, ext_names);

// Add debug ext
#ifdef VALIDATION
#ifndef __ANDROID__
    {
      assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif
#endif

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Toybox";
    app_info.applicationVersion = VK_MAKE_VERSION(
        HB_GAME_VERSION_MAJOR, HB_GAME_VERSION_MINOR, HB_GAME_VERSION_PATCH);
    app_info.pEngineName = HB_ENGINE_NAME;
    app_info.engineVersion =
        VK_MAKE_VERSION(HB_ENGINE_VERSION_MAJOR, HB_ENGINE_VERSION_MINOR,
                        HB_ENGINE_VERSION_PATCH);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;

    err = vkCreateInstance(&create_info, vk_alloc_ptr, &instance);
    assert(err == VK_SUCCESS);
    volkLoadInstance(instance);
  }

#ifdef VALIDATION
  VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
// Load debug callback
#ifndef __ANDROID__
  {
    VkDebugUtilsMessengerCreateInfoEXT ext_info = {0};
    ext_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ext_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ext_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ext_info.pfnUserCallback = vk_debug_callback;
    err = vkCreateDebugUtilsMessengerEXT(instance, &ext_info, vk_alloc_ptr,
                                         &debug_utils_messenger);
    assert(err == VK_SUCCESS);
  }
#endif
#endif

  Demo d = {0};
  bool success = demo_init(window, instance, std_alloc.alloc, arena.alloc,
                           vk_alloc_ptr, &d);
  assert(success);
  (void)success;

  SkyData sky_data = {
      .cirrus = 0.4,
      .cumulus = 0.8,
      .sun_dir = {0, -1, 0},
  };

  CommonCameraData camera_data = (CommonCameraData){0};

  // Query SDL for available display modes
  int32_t display_count = SDL_GetNumVideoDisplays();
  if (display_count < 1) {
    const char *msg = SDL_GetError();
    SDL_Log("Failed to enumerate displays with error: %s", msg);
    SDL_TriggerBreakpoint();
    return -1;
  }

  SDL_DisplayMode **modes_per_display =
      hb_alloc_nm_tp(std_alloc.alloc, display_count, SDL_DisplayMode *);
  char const **display_names =
      hb_alloc_nm_tp(std_alloc.alloc, display_count, const char *);

  char ***mode_names = hb_alloc_nm_tp(std_alloc.alloc, display_count, char **);

  for (int32_t i = 0; i < display_count; ++i) {
    int32_t display_mode_count = SDL_GetNumDisplayModes(i);

    modes_per_display[i] =
        hb_alloc_nm_tp(std_alloc.alloc, display_mode_count, SDL_DisplayMode);
    mode_names[i] = hb_alloc_nm_tp(std_alloc.alloc, display_mode_count, char *);
    for (int32_t ii = 0; ii < display_mode_count; ++ii) {
      SDL_DisplayMode mode = {0};
      if (SDL_GetDisplayMode(i, ii, &mode) != 0) {
        const char *msg = SDL_GetError();
        SDL_Log("Failed to get display mode with error: %s", msg);
        SDL_TriggerBreakpoint();
        return -1;
      }

      modes_per_display[i][ii] = mode;

      // Create name for display mode name
      static const uint32_t max_name_size = 512;
      mode_names[i][ii] = hb_alloc(std_alloc.alloc, max_name_size);
      SDL_snprintf(mode_names[i][ii], max_name_size, "%dx%d @%dHz", mode.w,
                   mode.h, mode.refresh_rate);
    }

    display_names[i] = SDL_GetDisplayName(i);
  }

  // Main loop
  bool running = true;

  bool showImGui = true;
  bool showSkyWindow = true;
  bool showSettingsWindow = true;
  bool showDemoWindow = false;
  bool showMetricsWindow = false;
  bool showSceneWindow = true;

  uint64_t time = 0;
  uint64_t start_time = SDL_GetPerformanceCounter();
  uint64_t last_time = SDL_GetPerformanceCounter();
  uint64_t delta_time = 0;
  float time_seconds = 0.0f;
  float delta_time_ms = 0.0f;
  float delta_time_seconds = 0.0f;

  // Controlled by ImGui and fed to the sky system
  float time_of_day = PI;
  float sun_y = cosf(PI + time_of_day);
  float sun_x = sinf(PI + time_of_day);

  while (running) {
    TracyCFrameMarkStart("Frame");
    TracyCZoneN(trcy_ctx, "Frame", true);
    TracyCZoneColor(trcy_ctx, TracyCategoryColorCore);

    // Use SDL High Performance Counter to get timing info
    time = SDL_GetPerformanceCounter() - start_time;
    delta_time = time - last_time;
    delta_time_seconds =
        (float)((double)delta_time / (double)SDL_GetPerformanceFrequency());
    time_seconds =
        (float)((double)time / (double)SDL_GetPerformanceFrequency());
    delta_time_ms = delta_time_seconds * 1000.0f;
    last_time = time;

    // TODO: Handle events more gracefully
    // Mutliple events (or none) could happen in one frame but we only process
    // the latest one

    // Make sure the camera's Aspect Ratio is always up to date
    main_cam.fov = (float)d.swap_info.width / (float)d.swap_info.height;

    // while (SDL_PollEvent(&e))
    {
      TracyCZoneN(ctx, "Handle Events", true);
      TracyCZoneColor(ctx, TracyCategoryColorInput);

      SDL_Event e = {0};
      {
        TracyCZoneN(sdl_ctx, "SDL_PollEvent", true);
        SDL_PollEvent(&e);
        TracyCZoneEnd(sdl_ctx);
      }
      if (e.type == SDL_QUIT) {
        running = false;
        TracyCZoneEnd(ctx);
        break;
      }
      demo_process_event(&d, &e);

      editor_camera_control(delta_time_seconds, &e, &controller, &main_cam);

      if (e.type == SDL_KEYDOWN) {
        const SDL_Keysym *keysym = &e.key.keysym;
        SDL_Scancode scancode = keysym->scancode;
        if (scancode == SDL_SCANCODE_GRAVE) {
          showImGui = !showImGui;
        }
      }

      TracyCZoneEnd(ctx);
    }

    ImVec2 display_size;
    display_size.x = d.swap_info.width;
    display_size.y = d.swap_info.height;
    d.ig_io->DisplaySize = display_size;
    d.ig_io->DeltaTime = delta_time_ms;
    igNewFrame();

    // ImGui Test

    if (showImGui) {
      TracyCZoneN(ctx, "UI Test", true);
      TracyCZoneColor(ctx, TracyCategoryColorUI);

      if (igBeginMainMenuBar()) {
        if (igBeginMenu("Sky", true)) {
          showSkyWindow = !showSkyWindow;
          igEndMenu();
        }
        if (igBeginMenu("Settings", true)) {
          showSettingsWindow = !showSettingsWindow;
          igEndMenu();
        }
        if (igBeginMenu("Metrics", true)) {
          showMetricsWindow = !showMetricsWindow;
          igEndMenu();
        }
        if (igBeginMenu("Demo", true)) {
          showDemoWindow = !showDemoWindow;
          igEndMenu();
        }
        if (igBeginMenu("Scene", true)) {
          showSceneWindow = !showSceneWindow;
          igEndMenu();
        }
        igEndMainMenuBar();
      }

      if (showSkyWindow && igBegin("Sky Control", &showSkyWindow, 0)) {
        if (igSliderFloat("Time of Day", &time_of_day, 0.0f, TAU, "%.3f", 0)) {
          sun_y = cosf(PI + time_of_day);
          sun_x = sinf(PI + time_of_day);
        }
        igSliderFloat("Cirrus", &sky_data.cirrus, 0.0f, 1.0f, "%.3f", 0);
        igSliderFloat("Cumulus", &sky_data.cumulus, 0.0f, 1.0f, "%.3f", 0);
        igEnd();
      }

      if (showSettingsWindow &&
          igBegin("Toybox Settings", &showSettingsWindow, 0)) {

        igLabelText("Frame Time (ms)", "%f", delta_time_ms);
        igLabelText("Framerate (fps)", "%f", (1000.0f / delta_time_ms));

        // WindowMode Combo Box
        {
          static int32_t window_sel = -1;
          if (window_sel == -1) {
            // Get current window mode selection
            for (uint32_t i = 0; i < TBWindowMode_Count; ++i) {
              if (settings.windowing_mode == TBWindowModes[i]) {
                window_sel = i;
                break;
              }
            }
          }
          if (igCombo_Str_arr("Window Mode", &window_sel, TBWindowModeNames,
                              TBWindowMode_Count, 0)) {
            settings.windowing_mode = TBWindowModes[window_sel];
          }
        }

        // Display Mode only matters for exclusive fullscreen
        if (settings.windowing_mode == TBWindowMode_Fullscreen) {
          static bool display_changed = false;
          if (display_changed == true) {
            display_changed = false;
          }
          // Display Combo Box
          if (igCombo_Str_arr("Display", &settings.display_index, display_names,
                              display_count, 0)) {
            // TODO: Display has changed!
            display_changed = true;
          }

          // DisplayMode Combo Box
          {
            int32_t mode_count = SDL_GetNumDisplayModes(settings.display_index);
            static int32_t mode_sel = -1;
            if (mode_sel == -1) {
              // Get the current display mode
              for (int32_t i = 0; i < mode_count; ++i) {
                SDL_DisplayMode *display_mode =
                    &modes_per_display[settings.display_index][i];
                if ((uint32_t)display_mode->w == settings.display_mode.width &&
                    (uint32_t)display_mode->h == settings.display_mode.height &&
                    (uint32_t)display_mode->refresh_rate ==
                        settings.display_mode.refresh_rate) {
                  mode_sel = i;
                  break;
                }
              }
            }
            if (display_changed || mode_sel == -1) {
              mode_sel = 0;
            }

            if (igCombo_Str_arr(
                    "Display Mode", &mode_sel,
                    (const char *const *)mode_names[settings.display_index],
                    mode_count, 0)) {
              // TODO: Display Mode changed
            }
          }
        }

        // Adaptive Resolution Slider
        if (igSliderFloat("Resolution Scale", &settings.resolution_scale, 0.2f,
                          2.0f, "%.4f", 0)) {
          // TODO: Resolution Scale has changed
        }

        // Vsync Combo Box
        {
          static int32_t vsync_sel = -1;
          if (vsync_sel == -1) {
            // Get current vsync selection
            for (uint32_t i = 0; i < TBVsync_Count; ++i) {
              if (settings.vsync_mode == TBVsyncModes[i]) {
                vsync_sel = i;
                break;
              }
            }
          }
          if (igCombo_Str_arr("Vsync", &vsync_sel, TBVsyncModeNames,
                              TBVsync_Count, 0)) {
            settings.vsync_mode = TBVsyncModes[vsync_sel];
          }
        }

        // MSAA Combo Box
        {
          static int32_t msaa_sel = -1;
          if (msaa_sel == -1) {
            // Get current msaa selection
            for (uint32_t i = 0; i < TBMSAAOptionCount; ++i) {
              if (settings.msaa == TBMSAAOptions[i]) {
                msaa_sel = i;
                break;
              }
            }
          }
          if (igCombo_Str_arr("MSAA", &msaa_sel, TBMSAAOptionNames,
                              TBMSAAOptionCount, 0)) {
            settings.msaa = TBMSAAOptions[msaa_sel];
          }
        }

        igEnd();
      }

      if (showDemoWindow) {
        igShowDemoWindow(&showDemoWindow);
      }
      if (showMetricsWindow) {
        igShowMetricsWindow(&showMetricsWindow);
      }

      if (showSceneWindow && igBegin("Scene Explorer", &showSceneWindow, 0)) {

        if (igTreeNode_StrStr("Entities", "Entity Count: %d",
                              d.main_scene->entity_count)) {
          for (uint32_t i = 0; i < d.main_scene->entity_count; ++i) {
            uint64_t components = d.main_scene->components[i];
            if (components & COMPONENT_TYPE_TRANSFORM) {
              SceneTransform *transform = &d.main_scene->transforms[i];
              igPushID_Ptr(transform);
              if (igTreeNode_StrStr("Transform", "%s", "Transform")) {
                {
                  float3 *position = &transform->t.position;
                  float x = (*position)[0];
                  float y = (*position)[1];
                  float z = (*position)[2];

                  igText("%s", "Position");
                  igSliderFloat("X", &x, -100.0f, 100.0f, "%.3f", 0);
                  if (x != (*position)[0]) {
                    (*position)[0] = x;
                  }
                  if (igSliderFloat("Y", &y, -100.0f, 100.0f, "%.3f", 0)) {
                    (*position)[1] = y;
                  }
                  if (igSliderFloat("Z", &z, -100.0f, 100.0f, "%.3f", 0)) {
                    (*position)[2] = z;
                  }
                }

                igSeparator();

                {
                  float3 *scale = &transform->t.scale;
                  float x = (*scale)[0];

                  igText("%s", "Uniform Scale");
                  igSliderFloat("scale", &x, 0.1f, 10.0f, "%.3f", 0);
                  if (x != (*scale)[0]) {
                    (*scale)[0] = x;
                    (*scale)[1] = x;
                    (*scale)[2] = x;
                  }
                }

                igTreePop();
              }
              igPopID();
            }

            if (components & COMPONENT_TYPE_STATIC_MESH) {
              igText("Static Mesh: %d", d.main_scene->static_mesh_refs[i]);
            }

            igSeparator();
          }
          igTreePop();
        }
        igText("Mesh Count: %d", d.main_scene->mesh_count);
        igText("Texture Count: %d", d.main_scene->texture_count);
        igText("Material Count: %d", d.main_scene->material_count);

        igEnd();
      }

      TracyCZoneEnd(ctx);
    }

    float4x4 view = {.row0 = {0}};
    camera_view(&main_cam, &view);

    float4x4 sky_view = {.row0 = {0}};
    camera_sky_view(&main_cam, &sky_view);

    float4x4 proj = {.row0 = {0}};
    camera_projection(&main_cam, &proj);

    float4x4 vp = {.row0 = {0}};
    mulmf44(&proj, &view, &vp);

    float4x4 sky_vp = {.row0 = {0}};
    mulmf44(&proj, &sky_view, &sky_vp);

    // Change sun position
    sky_data.sun_dir = normf3((float3){sun_x, sun_y, 0});
    sky_data.time = time_seconds;

    // Calculate sun view proj matrix for shadow mapping
    float4x4 sun_vp = {.row0 = {0}};
    {
      float3 sun_pos = sky_data.sun_dir * -50.0f;

      float4x4 sun_view = {.row0 = {0}};
      look_at(&sun_view, sun_pos, (float3){0}, (float3){0, 1, 0});

      float4x4 sun_proj = {.row0 = {0}};
      orthographic(&sun_proj, 128, 128, 0.1f, 100.0f);

      mulmf44(&sun_proj, &sun_view, &sun_vp);
    }

    // Update view camera constant buffer
    {
      TracyCZoneN(trcy_camera_ctx, "Update Camera Const Buffer", true);
      camera_data.vp = vp;
      // TODO: camera_data.inv_vp = inv_vp;
      camera_data.view_pos = main_cam.transform.position;

      VmaAllocator vma_alloc = d.vma_alloc;
      VmaAllocation camera_host_alloc = d.camera_const_buffer.host.alloc;

      uint8_t *data = NULL;
      err = vmaMapMemory(vma_alloc, camera_host_alloc, (void **)&data);
      if (err != VK_SUCCESS) {
        assert(0);
        return false;
      }

      memcpy(data, &camera_data, sizeof(CommonCameraData));
      vmaUnmapMemory(vma_alloc, camera_host_alloc);

      demo_upload_const_buffer(&d, &d.camera_const_buffer);

      TracyCZoneEnd(trcy_camera_ctx);
    }

    // Update view light constant buffer
    {
      TracyCZoneN(trcy_light_ctx, "Update Light Const Buffer", true);

      CommonLightData light_data = {
          .light_dir = -sky_data.sun_dir,
          .light_vp = sun_vp,
      };

      VmaAllocator vma_alloc = d.vma_alloc;
      VmaAllocation light_host_alloc = d.light_const_buffer.host.alloc;

      uint8_t *data = NULL;
      err = vmaMapMemory(vma_alloc, light_host_alloc, (void **)&data);
      if (err != VK_SUCCESS) {
        assert(0);
        return false;
      }
      // HACK: just pluck the light direction from the push constants for now
      memcpy(data, &light_data, sizeof(CommonLightData));
      vmaUnmapMemory(vma_alloc, light_host_alloc);

      demo_upload_const_buffer(&d, &d.light_const_buffer);

      TracyCZoneEnd(trcy_light_ctx);
    }

    // Update sky constant buffer
    {
      TracyCZoneN(trcy_sky_ctx, "Update Sky", true);

      VmaAllocator vma_alloc = d.vma_alloc;
      VmaAllocation sky_host_alloc = d.sky_const_buffer.host.alloc;

      uint8_t *data = NULL;
      err = vmaMapMemory(vma_alloc, sky_host_alloc, (void **)&data);
      if (err != VK_SUCCESS) {
        assert(0);
        return false;
      }
      memcpy(data, &sky_data, sizeof(SkyData));
      vmaUnmapMemory(vma_alloc, sky_host_alloc);

      demo_upload_const_buffer(&d, &d.sky_const_buffer);
      TracyCZoneEnd(trcy_sky_ctx);
    }

    demo_render_frame(&d, &vp, &sky_vp, &sun_vp);

    // Reset the arena allocator
    arena = reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx);
    TracyCFrameMarkEnd("Frame");
  }

  // Cleanup display modes
  for (int32_t i = 0; i < display_count; ++i) {
    int32_t mode_count = SDL_GetNumDisplayModes(i);
    hb_free(std_alloc.alloc, modes_per_display[i]);
    for (int32_t ii = 0; ii < mode_count; ++ii) {
      hb_free(std_alloc.alloc, mode_names[i][ii]);
    }
    hb_free(std_alloc.alloc, mode_names[i]);
  }
  hb_free(std_alloc.alloc, modes_per_display);
  hb_free(std_alloc.alloc, display_names);

  SDL_DestroyWindow(window);
  window = NULL;

  IMG_Quit();
  SDL_Quit();

  demo_destroy(&d);

#ifdef VALIDATION
#ifndef __ANDROID__
  vkDestroyDebugUtilsMessengerEXT(instance, debug_utils_messenger,
                                  vk_alloc_ptr);
  debug_utils_messenger = VK_NULL_HANDLE;
#endif
#endif

  vkDestroyInstance(instance, vk_alloc_ptr);
  instance = VK_NULL_HANDLE;

  destroy_arena_allocator(arena);
  destroy_standard_allocator(std_alloc);
  mi_heap_delete(vk_heap);

  return 0;
}
