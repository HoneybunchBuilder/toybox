#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

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
#include "tbsdl.h"
#include "tbvk.h"
#include "tbvma.h"

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#if !defined(FINAL) && !defined(__ANDROID__)
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
  TracyCFreeN(pMemory, "Vulkan") mi_free(pMemory);
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
  {
    const char *app_info = TB_APP_INFO_STR;
    size_t app_info_len = strlen(app_info);
    TracyCAppInfo(app_info, app_info_len)(void) app_info_len;
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
      .fov = PI_2,
      .near = 0.01f,
      .far = 1000.0f,
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
        VkLayerProperties *instance_layers = tb_alloc_nm_tp(
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
    {
      assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif

// Add portability for apple devices
#ifdef __APPLE__
    {
      assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    }
#endif

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Toybox";
    app_info.applicationVersion = VK_MAKE_VERSION(
        TB_GAME_VERSION_MAJOR, TB_GAME_VERSION_MINOR, TB_GAME_VERSION_PATCH);
    app_info.pEngineName = TB_ENGINE_NAME;
    app_info.engineVersion =
        VK_MAKE_VERSION(TB_ENGINE_VERSION_MAJOR, TB_ENGINE_VERSION_MINOR,
                        TB_ENGINE_VERSION_PATCH);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    // Only use this portability bit when necessary. Some older system
    // header versions of vulkan may not support it.
#if defined(VK_USE_PLATFORM_MACOS_MVK) && (VK_HEADER_VERSION >= 216)
    create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;

    err = vkCreateInstance(&create_info, vk_alloc_ptr, &instance);
    assert(err == VK_SUCCESS);
    volkLoadInstance(instance);
  }

// Load debug callback
#ifdef VALIDATION
  VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
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

  Demo d = {0};
  bool success = demo_init(window, instance, std_alloc.alloc, arena.alloc,
                           vk_alloc_ptr, &d);
  assert(success);
  (void)success;

  // Get scene asset paths
  const char **scene_asset_paths =
      tb_alloc_nm_tp(d.tmp_alloc, tb_scene_database_num, const char *);
  for (uint32_t i = 0; i < tb_scene_database_num; ++i) {
    const uint32_t scene_idx = tb_scene_database[i];
    SDL_assert(scene_idx < tb_asset_database_num);
    scene_asset_paths[i] = tb_asset_database[scene_idx];
  }

  // Load starter scene
  int32_t scene_idx = 4;
  const char *scene_path = scene_asset_paths[scene_idx];
  demo_load_scene(&d, scene_path);

  SkyData sky_data = {
      .cirrus = 0.4f,
      .cumulus = 0.8f,
      .sun_dir = {0, -1, 0},
  };

  CommonCameraData camera_data = (CommonCameraData){0};

  // Query SDL for available display modes
  uint32_t display_count = (uint32_t)SDL_GetNumVideoDisplays();
  if (display_count < 1) {
    const char *msg = SDL_GetError();
    SDL_Log("Failed to enumerate displays with error: %s", msg);
    SDL_TriggerBreakpoint();
    return -1;
  }

  SDL_DisplayMode **modes_per_display =
      tb_alloc_nm_tp(std_alloc.alloc, display_count, SDL_DisplayMode *);
  char const **display_names =
      tb_alloc_nm_tp(std_alloc.alloc, display_count, const char *);

  char ***mode_names = tb_alloc_nm_tp(std_alloc.alloc, display_count, char **);

  for (uint32_t i = 0; i < display_count; ++i) {
    uint32_t display_mode_count = (uint32_t)SDL_GetNumDisplayModes((int32_t)i);

    modes_per_display[i] =
        tb_alloc_nm_tp(std_alloc.alloc, display_mode_count, SDL_DisplayMode);
    mode_names[i] = tb_alloc_nm_tp(std_alloc.alloc, display_mode_count, char *);
    for (uint32_t ii = 0; ii < display_mode_count; ++ii) {
      SDL_DisplayMode mode = {0};
      if (SDL_GetDisplayMode((int32_t)i, (int32_t)ii, &mode) != 0) {
        const char *msg = SDL_GetError();
        SDL_Log("Failed to get display mode with error: %s", msg);
        SDL_TriggerBreakpoint();
        return -1;
      }

      modes_per_display[i][ii] = mode;

      // Create name for display mode name
      static const uint32_t max_name_size = 512;
      mode_names[i][ii] = tb_alloc(std_alloc.alloc, max_name_size);
      SDL_snprintf(mode_names[i][ii], max_name_size, "%dx%d @%dHz", mode.w,
                   mode.h, mode.refresh_rate);
    }

    display_names[i] = SDL_GetDisplayName((int32_t)i);
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
        (float)((double)delta_time / (double)(SDL_GetPerformanceFrequency()));
    time_seconds =
        (float)((double)time / (double)(SDL_GetPerformanceFrequency()));
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
    display_size.x = (float)d.swap_info.width;
    display_size.y = (float)d.swap_info.height;
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

        igLabelText("Frame Time (ms)", "%f", (double)delta_time_ms);
        igLabelText("Framerate (fps)", "%f", (double)(1000.0f / delta_time_ms));

        // WindowMode Combo Box
        {
          static int32_t window_sel = -1;
          if (window_sel == -1) {
            // Get current window mode selection
            for (int32_t i = 0; i < TBWindowMode_Count; ++i) {
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
                              (int32_t)display_count, 0)) {
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
            for (int32_t i = 0; i < TBVsync_Count; ++i) {
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
            for (int32_t i = 0; i < TBMSAAOptionCount; ++i) {
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

        // Device Details
        if (igTreeNode_StrStr("DeviceDetails", "Device Details")) {
          {
            const uint32_t major = VK_API_VERSION_MAJOR(d.gpu_props.apiVersion);
            const uint32_t minor = VK_API_VERSION_MINOR(d.gpu_props.apiVersion);
            const uint32_t patch = VK_API_VERSION_PATCH(d.gpu_props.apiVersion);
            const uint32_t variant =
                VK_API_VERSION_VARIANT(d.gpu_props.apiVersion);

            igText("API Version:\t   %" SDL_PRIu32 ".%" SDL_PRIu32
                   ".%" SDL_PRIu32 ".%" SDL_PRIu32,
                   major, minor, patch, variant);
          }
          igText("Driver Version:\t%" SDL_PRIx32, d.gpu_props.driverVersion);

          // According to
          // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceProperties.html
          // The low 16 bits of the vendor id is a pcie vendor id.
          // If the vendor id is outside this range, it's a KHR specified
          // vendor id found in VkVendorId;
          if (d.gpu_props.vendorID <= 0x0000FFFF) {
            const uint32_t pcie_vendor_id = d.gpu_props.vendorID & 0x0000FFFF;
            igText("Vendor ID:\t\t PCIe(%" SDL_PRIx32 ")", pcie_vendor_id);
          } else {
            switch (d.gpu_props.vendorID) {
            case VK_VENDOR_ID_VIV:
              igText("Vendor ID:\t\t %s", "VIV");
              break;
            case VK_VENDOR_ID_VSI:
              igText("Vendor ID:\t\t %s", "VSI");
              break;
            case VK_VENDOR_ID_KAZAN:
              igText("Vendor ID:\t\t %s", "Kazan");
              break;
            case VK_VENDOR_ID_CODEPLAY:
              igText("Vendor ID:\t\t %s", "Codeplay");
              break;
            case VK_VENDOR_ID_MESA:
              igText("Vendor ID:\t\t %s", "Mesa");
              break;
            case VK_VENDOR_ID_POCL:
              igText("Vendor ID:\t\t %s", "POCL");
              break;
            default:
              igText("Vendor ID:\t\t %s", "Unknown");
              break;
            }
          }

          igText("Device ID:\t\t %" SDL_PRIx32, d.gpu_props.deviceID);

          switch (d.gpu_props.deviceType) {
          case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            igText("Device Type:\t   %s", "Other");
            break;
          case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            igText("Device Type:\t   %s", "Integrated GPU");
            break;
          case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            igText("Device Type:\t   %s", "Discrete GPU");
            break;
          case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            igText("Device Type:\t   %s", "Virtual GPU");
            break;
          case VK_PHYSICAL_DEVICE_TYPE_CPU:
            igText("Device Type:\t   %s", "CPU");
            break;
          default:
            igText("Device Type:\t   %s", "Unknown");
            break;
          }
          igText("Device Name:\t   %s", d.gpu_props.deviceName);

          igTreePop();
        }

        // Driver Details
        if (igTreeNode_StrStr("DriverDetails", "Driver Details")) {
          switch (d.driver_props.driverID) {
          case VK_DRIVER_ID_AMD_PROPRIETARY:
            igText("Driver ID:\t  %s", "AMD Proprietary");
            break;
          case VK_DRIVER_ID_AMD_OPEN_SOURCE:
            igText("Driver ID:\t  %s", "AMD Open Source");
            break;
          case VK_DRIVER_ID_MESA_RADV:
            igText("Driver ID:\t  %s", "MESA RADV");
            break;
          case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
            igText("Driver ID:\t  %s", "NVIDIA Proprietary");
            break;
          case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
            igText("Driver ID:\t  %s", "Intel Proprietary Windows");
            break;
          case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
            igText("Driver ID:\t  %s", "Intel Open Source Mesa");
            break;
          case VK_DRIVER_ID_IMAGINATION_PROPRIETARY:
            igText("Driver ID:\t  %s", "Imagination Proprietary");
            break;
          case VK_DRIVER_ID_QUALCOMM_PROPRIETARY:
            igText("Driver ID:\t  %s", "Qualcomm Proprietary");
            break;
          case VK_DRIVER_ID_ARM_PROPRIETARY:
            igText("Driver ID:\t  %s", "ARM Proprietary");
            break;
          case VK_DRIVER_ID_GOOGLE_SWIFTSHADER:
            igText("Driver ID:\t  %s", "Google Swiftshader");
            break;
          case VK_DRIVER_ID_GGP_PROPRIETARY:
            igText("Driver ID:\t  %s", "GGP Proprietary");
            break;
          case VK_DRIVER_ID_BROADCOM_PROPRIETARY:
            igText("Driver ID:\t  %s", "Broadcom Proprietary");
            break;
          case VK_DRIVER_ID_MESA_LLVMPIPE:
            igText("Driver ID:\t  %s", "MESA LLVMPipe");
            break;
          case VK_DRIVER_ID_MOLTENVK:
            igText("Driver ID:\t  %s", "MoltenVK Proprietary");
            break;
          case VK_DRIVER_ID_COREAVI_PROPRIETARY:
            igText("Driver ID:\t  %s", "CoreAVI Proprietary");
            break;
          case VK_DRIVER_ID_JUICE_PROPRIETARY:
            igText("Driver ID:\t  %s", "Juice Proprietary");
            break;
          case VK_DRIVER_ID_VERISILICON_PROPRIETARY:
            igText("Driver ID:\t  %s", "Verisilicon Proprietary");
            break;
          case VK_DRIVER_ID_MESA_TURNIP:
            igText("Driver ID:\t  %s", "MESA Turnip");
            break;
          case VK_DRIVER_ID_MESA_V3DV:
            igText("Driver ID:\t  %s", "MESA V3DV");
            break;
          case VK_DRIVER_ID_MESA_PANVK:
            igText("Driver ID:\t  %s", "MESA PanVK");
            break;
          case VK_DRIVER_ID_SAMSUNG_PROPRIETARY:
            igText("Driver ID:\t  %s", "Samsung Proprietary");
            break;
          case VK_DRIVER_ID_MESA_VENUS:
            igText("Driver ID:\t  %s", "MESA Venus");
            break;
          default:
            igText("Driver ID:\t  %s", "Unknown");
            break;
          }

          igText("Driver Name:\t%s", d.driver_props.driverName);
          igText("Driver Info:\t%s", d.driver_props.driverInfo);

          igTreePop();
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
        igText("Current Scene: ");
        if (d.main_scene->loaded) {
          igSameLine(0.0f, -1.0f);
          igText("%s", scene_path);
        }

        // Combo for scene selection
        {
          int32_t selected_scene_index = scene_idx;
          const char *preview =
              scene_idx < 0 ? "" : scene_asset_paths[scene_idx];
          if (igBeginCombo("Scenes", preview, 0)) {
            for (int32_t i = 0; i < (int32_t)tb_scene_database_num; ++i) {
              const bool is_selected = (scene_idx == i);
              if (igSelectable_Bool(scene_asset_paths[i], is_selected, 0,
                                    (ImVec2){0})) {
                selected_scene_index = i;
              }

              if (is_selected) {
                igSetItemDefaultFocus();
              }
            }
            igEndCombo();
          }

          if (selected_scene_index != scene_idx) {
            scene_idx = selected_scene_index;
            scene_path = scene_asset_paths[scene_idx];
            demo_unload_scene(&d);
            demo_load_scene(&d, scene_path);
          }
        }

        // Button is disabled if we don't have a scene open
        {
          const float unload_button_alpha = d.main_scene->loaded ? 1.0f : 0.5f;

          igPushItemFlag(ImGuiItemFlags_Disabled, !d.main_scene->loaded);
          igPushStyleVar_Float(ImGuiStyleVar_Alpha, unload_button_alpha);

          if (igButton("Unload Scene", (ImVec2){0})) {
            scene_idx = -1;
            scene_path = "";
            demo_unload_scene(&d);
          }
          igPopStyleVar(1);
          igPopItemFlag();
        }

        if (d.main_scene->loaded) {
          if (igTreeNode_StrStr("Entities", "Entity Count: %d",
                                d.main_scene->entity_count)) {
            for (uint32_t i = 0; i < d.main_scene->entity_count; ++i) {
              uint64_t components = d.main_scene->components[i];
              if (components & COMPONENT_TYPE_TRANSFORM) {
                SceneTransform *transform = &d.main_scene->transforms[i];
                igPushID_Ptr(transform);
                if (igTreeNode_StrStr("Transform", "%s", "Transform")) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
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

#ifdef __clang__
#pragma clang diagnostic pop
#endif
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
        }

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
      float radius = 80.0f;
      float3 sun_pos = sky_data.sun_dir * (-radius * 0.5f);

      float4x4 sun_view = {.row0 = {0}};
      look_at(&sun_view, sun_pos, (float3){0}, (float3){0, 1, 0});

      float4x4 sun_proj = {.row0 = {0}};
      orthographic(&sun_proj, radius, radius, 0.0f, radius * 2);

      mulmf44(&sun_proj, &sun_view, &sun_vp);
    }

    // Update view camera constant buffer
    {
      camera_data.vp = vp;
      // TODO: camera_data.inv_vp = inv_vp;
      camera_data.view_pos = main_cam.transform.position;
      demo_set_camera(&d, &camera_data);
    }

    // Update view light constant buffer
    {
      CommonLightData light_data = {
          .light_dir = -sky_data.sun_dir,
          .light_vp = sun_vp,
      };

      demo_set_sun(&d, &light_data);
    }

    // Update sky constant buffer
    demo_set_sky(&d, &sky_data);

    demo_render_frame(&d, &vp, &sky_vp, &sun_vp);

    // Reset the arena allocator
    arena = reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx) TracyCFrameMarkEnd("Frame");
  }

  // Cleanup display modes
  for (int32_t i = 0; i < (int32_t)display_count; ++i) {
    int32_t mode_count = SDL_GetNumDisplayModes(i);
    tb_free(std_alloc.alloc, modes_per_display[i]);
    for (int32_t ii = 0; ii < mode_count; ++ii) {
      tb_free(std_alloc.alloc, mode_names[i][ii]);
    }
    tb_free(std_alloc.alloc, mode_names[i]);
  }
  tb_free(std_alloc.alloc, modes_per_display);
  tb_free(std_alloc.alloc, display_names);

  SDL_DestroyWindow(window);
  window = NULL;

  demo_destroy(&d);

#ifdef VALIDATION
  vkDestroyDebugUtilsMessengerEXT(instance, debug_utils_messenger,
                                  vk_alloc_ptr);
  debug_utils_messenger = VK_NULL_HANDLE;
#endif

  vkDestroyInstance(instance, vk_alloc_ptr);
  instance = VK_NULL_HANDLE;

  IMG_Quit();
  SDL_Quit();

  destroy_arena_allocator(arena);
  destroy_standard_allocator(std_alloc);
  mi_heap_delete(vk_heap);

  return 0;
}
