#include "demo.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <float.h>
#include <stddef.h>

#include <volk.h>

#include "vk_mem_alloc.h"

#include "cpuresources.h"
#include "hosek.h"
#include "pipelines.h"
#include "shadercommon.h"
#include "simd.h"
#include "skydome.h"
#include "vkdbg.h"

#ifdef __ANDROID__
#define ASSET_PREFIX
#elif __SWITCH__
#define ASSET_PREFIX "romfs:/"
#else
#define ASSET_PREFIX "./assets/"
#endif

#define MAX_EXT_COUNT 16

static void vma_alloc_fn(VmaAllocator allocator, uint32_t memoryType,
                         VkDeviceMemory memory, VkDeviceSize size,
                         void *pUserData) {
  (void)allocator;
  (void)memoryType;
  (void)memory;
  (void)size;
  (void)pUserData;
  TracyCAllocN((void *)memory, size, "VMA");
}
static void vma_free_fn(VmaAllocator allocator, uint32_t memoryType,
                        VkDeviceMemory memory, VkDeviceSize size,
                        void *pUserData) {
  (void)allocator;
  (void)memoryType;
  (void)memory;
  (void)size;
  (void)pUserData;
  TracyCFreeN((void *)memory, "VMA");
}

static VkDevice create_device(VkPhysicalDevice gpu,
                              uint32_t graphics_queue_family_index,
                              uint32_t present_queue_family_index,
                              uint32_t ext_count,
                              const VkAllocationCallbacks *vk_alloc,
                              const char *const *ext_names) {
  TracyCZoneN(ctx, "create_device", true);

  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queues[2];
  queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queues[0].pNext = NULL;
  queues[0].queueFamilyIndex = graphics_queue_family_index;
  queues[0].queueCount = 1;
  queues[0].pQueuePriorities = queue_priorities;
  queues[0].flags = 0;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipe_feature = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
      .rayTracingPipeline = VK_TRUE,
  };

  VkDeviceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
  create_info.pNext = (const void *)&rt_pipe_feature;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = queues;
  create_info.enabledExtensionCount = ext_count;
  create_info.ppEnabledExtensionNames = ext_names;

  if (present_queue_family_index != graphics_queue_family_index) {
    queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[1].pNext = NULL;
    queues[1].queueFamilyIndex = present_queue_family_index;
    queues[1].queueCount = 1;
    queues[1].pQueuePriorities = queue_priorities;
    queues[1].flags = 0;
    create_info.queueCreateInfoCount = 2;
  }

  VkDevice device = VK_NULL_HANDLE;
  VkResult err = vkCreateDevice(gpu, &create_info, vk_alloc, &device);
  assert(err == VK_SUCCESS);
  (void)err;

  TracyCZoneEnd(ctx);

  return device;
}

static VkPhysicalDevice select_gpu(VkInstance instance, Allocator tmp_alloc) {
  TracyCZoneN(ctx, "select_gpu", true);

  uint32_t gpu_count = 0;
  VkResult err = vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
  assert(err == VK_SUCCESS);
  (void)err;

  VkPhysicalDevice *physical_devices =
      hb_alloc_nm_tp(tmp_alloc, gpu_count, VkPhysicalDevice);
  err = vkEnumeratePhysicalDevices(instance, &gpu_count, physical_devices);
  assert(err == VK_SUCCESS);

  /* Try to auto select most suitable device */
  int32_t gpu_idx = -1;
  {
    uint32_t count_device_type[VK_PHYSICAL_DEVICE_TYPE_CPU + 1];
    memset(count_device_type, 0, sizeof(count_device_type));

    VkPhysicalDeviceProperties physicalDeviceProperties;
    for (uint32_t i = 0; i < gpu_count; i++) {
      vkGetPhysicalDeviceProperties(physical_devices[i],
                                    &physicalDeviceProperties);
      assert(physicalDeviceProperties.deviceType <=
             VK_PHYSICAL_DEVICE_TYPE_CPU);
      count_device_type[physicalDeviceProperties.deviceType]++;
    }

    VkPhysicalDeviceType search_for_device_type =
        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_CPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_CPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_OTHER]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    }

    for (uint32_t i = 0; i < gpu_count; i++) {
      vkGetPhysicalDeviceProperties(physical_devices[i],
                                    &physicalDeviceProperties);
      if (physicalDeviceProperties.deviceType == search_for_device_type) {
        gpu_idx = i;
        break;
      }
    }
  }
  assert(gpu_idx >= 0);
  VkPhysicalDevice gpu = physical_devices[gpu_idx];
  hb_free(tmp_alloc, physical_devices);

  TracyCZoneEnd(ctx);

  return gpu;
}

static VkSurfaceFormatKHR
pick_surface_format(VkSurfaceFormatKHR *surface_formats,
                    uint32_t format_count) {
  // Prefer non-SRGB formats...
  for (uint32_t i = 0; i < format_count; i++) {
    const VkFormat format = surface_formats[i].format;

    if (format == VK_FORMAT_R8G8B8A8_UNORM ||
        format == VK_FORMAT_B8G8R8A8_UNORM ||
        format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
        format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 ||
        format == VK_FORMAT_R16G16B16A16_SFLOAT) {
      return surface_formats[i];
    }
  }

  assert(format_count >= 1);
  return surface_formats[0];
}

static void demo_render_scene(Scene *s, VkCommandBuffer cmd,
                              VkPipelineLayout layout, VkDescriptorSet view_set,
                              VkDescriptorSet *object_sets,
                              VkDescriptorSet *material_sets,
                              const float4x4 *vp, Demo *d) {
  TracyCZoneN(ctx, "demo_render_scene", true);
  TracyCZoneColor(ctx, TracyCategoryColorRendering);

  // HACK: Upload all material const buffers every frame
  {
    TracyCZoneN(mat_up_ctx, "material data upload", true);
    for (uint32_t i = 0; i < s->material_count; ++i) {
      demo_upload_const_buffer(d, &s->materials[i].const_buffer);
    }
    TracyCZoneEnd(mat_up_ctx);
  }

  // Bind per-view data - TODO: we should do this somewhere else
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 1,
                          &view_set, 0, NULL);

  for (uint32_t i = 0; i < s->entity_count; ++i) {
    uint64_t components = s->components[i];
    SceneTransform *scene_transform = &s->transforms[i];
    uint32_t static_mesh_idx = s->static_mesh_refs[i];
    uint32_t material_idx = s->material_refs[i];

    if (components & COMPONENT_TYPE_STATIC_MESH) {
      Transform *t = &scene_transform->t;

      // Hack to fuck with the scale of the object
      // t->scale = (float3){0.01f, -0.01f, 0.01f};
      // t->scale = (float3){100.0f, -100.0f, 100.0f};
      t->scale = (float3){1.0f, -1.0f, 1.0f};

      CommonObjectData object_data = {0};

      transform_to_matrix(&object_data.m, t);
      mulmf44(vp, &object_data.m, &object_data.mvp);

      // HACK: Update object's constant buffer here
      {
        TracyCZoneN(update_object_ctx, "Update Object Const Buffer", true);
        TracyCZoneColor(update_object_ctx, TracyCategoryColorRendering);

        uint32_t block_idx = i / CONST_BUFFER_BLOCK_SIZE;
        uint32_t item_idx = i % CONST_BUFFER_BLOCK_SIZE;

        GPUConstBuffer *obj_const_buffer =
            &d->obj_const_buffer_blocks[block_idx][item_idx];

        VmaAllocator vma_alloc = d->vma_alloc;
        VmaAllocation object_host_alloc = obj_const_buffer->host.alloc;

        uint8_t *data = NULL;
        VkResult err =
            vmaMapMemory(vma_alloc, object_host_alloc, (void **)&data);
        if (err != VK_SUCCESS) {
          assert(0);
          return;
        }
        memcpy(data, &object_data, sizeof(CommonObjectData));
        vmaUnmapMemory(vma_alloc, object_host_alloc);

        demo_upload_const_buffer(d, obj_const_buffer);

        TracyCZoneEnd(update_object_ctx);
      }

      cmd_begin_label(cmd, "demo_render_scene", (float4){0.5, 0.1, 0.1, 1.0});

      // Bind per-object data
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                              1, &object_sets[i], 0, NULL);

      const GPUMesh *mesh = &s->meshes[static_mesh_idx];

      // Draw mesh surfaces
      for (uint32_t ii = 0; ii < mesh->surface_count; ++ii) {

        // TODO: Bind per-surface material
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                                1, &material_sets[material_idx], 0, NULL);

        const GPUSurface *surface = &mesh->surfaces[ii];

        uint32_t idx_count = surface->idx_count;
        uint32_t vtx_count = surface->vtx_count;
        VkBuffer buffer = surface->gpu.buffer;

        vkCmdBindIndexBuffer(cmd, buffer, 0, VK_INDEX_TYPE_UINT16);
        VkDeviceSize offset = surface->idx_size;

        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
        offset += vtx_count * sizeof(float) * 3;

        vkCmdBindVertexBuffers(cmd, 1, 1, &buffer, &offset);
        offset += vtx_count * sizeof(float) * 3;

        vkCmdBindVertexBuffers(cmd, 2, 1, &buffer, &offset);

        vkCmdDrawIndexed(cmd, idx_count, 1, 0, 0, 0);
      }

      cmd_end_label(cmd);
    }
  }
  TracyCZoneEnd(ctx);
}

static void demo_imgui_update(Demo *d) {
  ImGuiIO *io = d->ig_io;
  // ImVec2 mouse_pos_prev = io->MousePos;
  io->MousePos = (ImVec2){-FLT_MAX, -FLT_MAX};

  // Update mouse buttons
  int32_t mouse_x_local = 0;
  int32_t mouse_y_local = 0;
  uint32_t mouse_buttons = SDL_GetMouseState(&mouse_x_local, &mouse_y_local);
  // If a mouse press event came, always pass it as "mouse held this
  // frame", so we don't miss click-release events that are shorter
  // than 1 frame.
  io->MouseDown[0] = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
  io->MouseDown[1] = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
  io->MouseDown[2] = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
  // bd->MousePressed[0] = bd->MousePressed[1] = bd->MousePressed[2] = false;

  // SDL_Window *mouse_window = NULL;

  // Obtain focused and hovered window. We forward mouse input when focused or
  // when hovered (and no other window is capturing)
  /*
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
  SDL_Window *focused_window = SDL_GetKeyboardFocus();
  SDL_Window *hovered_window =
      SDL_HAS_MOUSE_FOCUS_CLICKTHROUGH
          ? SDL_GetMouseFocus()
          : NULL; // This is better but is only reliably useful with SDL 2.0.5+
                  // and SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH.

  if (hovered_window && bd->Window == hovered_window)
    mouse_window = hovered_window;
  else if (focused_window && bd->Window == focused_window)
    mouse_window = focused_window;

  // SDL_CaptureMouse() let the OS know e.g. that our imgui drag outside the SDL
  // window boundaries shouldn't e.g. trigger other operations outside
  SDL_CaptureMouse(ImGui::IsAnyMouseDown() ? SDL_TRUE : SDL_FALSE);
#else
  // SDL 2.0.3 and non-windowed systems: single-viewport only
  SDL_Window *mouse_window =
      (SDL_GetWindowFlags(bd->Window) & SDL_WINDOW_INPUT_FOCUS) ? bd->Window
                                                                : NULL;
#endif
 */

  // if (mouse_window == NULL)
  //  return;

  // Set OS mouse position from Dear ImGui if requested (rarely used, only when
  // ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
  // if (io->WantSetMousePos)
  //  SDL_WarpMouseInWindow(bd->Window, (int32_t)mouse_pos_prev.x,
  //                        (int32_t)mouse_pos_prev.y);

  // Set Dear ImGui mouse position from OS position + get buttons. (this is the
  // common behavior)
  /*
  if (bd->MouseCanUseGlobalState) {
    // Single-viewport mode: mouse position in client window coordinates
    // (io->MousePos is (0,0) when the mouse is on the upper-left corner of the
    // app window) Unlike local position obtained earlier this will be valid
    // when straying out of bounds.
    int32_t mouse_x_global, mouse_y_global;
    SDL_GetGlobalMouseState(&mouse_x_global, &mouse_y_global);
    int32_t window_x, window_y;
    SDL_GetWindowPosition(mouse_window, &window_x, &window_y);
    io->MousePos = ImVec2((float)(mouse_x_global - window_x),
                          (float)(mouse_y_global - window_y));
  } else
  */
  { io->MousePos = (ImVec2){(float)mouse_x_local, (float)mouse_y_local}; }
}

static SwapchainInfo init_swapchain(SDL_Window *window, VkDevice device,
                                    VkPhysicalDevice gpu, VkSurfaceKHR surface,
                                    VkSwapchainKHR *swapchain,
                                    const VkAllocationCallbacks *vk_alloc,
                                    Allocator tmp_alloc) {
  SwapchainInfo swap_info = {0};

  int32_t width = 0;
  int32_t height = 0;
  SDL_Vulkan_GetDrawableSize(window, &width, &height);

  uint32_t format_count = 0;
  VkResult err =
      vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
  assert(err == VK_SUCCESS);
  (void)err;
  VkSurfaceFormatKHR *surface_formats =
      hb_alloc_nm_tp(tmp_alloc, format_count, VkSurfaceFormatKHR);
  err = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count,
                                             surface_formats);
  assert(err == VK_SUCCESS);
  VkSurfaceFormatKHR surface_format =
      pick_surface_format(surface_formats, format_count);

  VkSurfaceCapabilitiesKHR surf_caps;
  err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surf_caps);
  assert(err == VK_SUCCESS);

  uint32_t present_mode_count = 0;
  err = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface,
                                                  &present_mode_count, NULL);
  assert(err == VK_SUCCESS);
  VkPresentModeKHR *present_modes =
      hb_alloc_nm_tp(tmp_alloc, present_mode_count, VkPresentModeKHR);
  assert(present_modes);
  err = vkGetPhysicalDeviceSurfacePresentModesKHR(
      gpu, surface, &present_mode_count, present_modes);
  assert(err == VK_SUCCESS);

  VkExtent2D swapchain_extent = {
      .width = (uint32_t)width,
      .height = (uint32_t)height,
  };

  // The FIFO present mode is guaranteed by the spec to be supported
  // and to have no tearing.  It's a great default present mode to use.
  VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  if (present_mode != swapchain_present_mode) {
    for (size_t i = 0; i < present_mode_count; ++i) {
      if (present_modes[i] == present_mode) {
        swapchain_present_mode = present_mode;
        break;
      }
    }
  }
  if (swapchain_present_mode != present_mode) {
    // The desired present mode was not found, just use the first one
    present_mode = present_modes[0];
  }
  hb_free(tmp_alloc, present_modes);
  present_modes = NULL;

  // Determine the number of VkImages to use in the swap chain.
  // Application desires to acquire 3 images at a time for triple
  // buffering
  uint32_t image_count = FRAME_LATENCY;
  if (image_count < surf_caps.minImageCount) {
    image_count = surf_caps.minImageCount;
  }
  // If maxImageCount is 0, we can ask for as many images as we want;
  // otherwise we're limited to maxImageCount
  if ((surf_caps.maxImageCount > 0) &&
      (image_count > surf_caps.maxImageCount)) {
    // Application must settle for fewer images than desired:
    image_count = surf_caps.maxImageCount;
  }

  VkSurfaceTransformFlagsKHR pre_transform;
  if (surf_caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  } else {
    pre_transform = surf_caps.currentTransform;
  }

  // Find a supported composite alpha mode - one of these is guaranteed to
  // be set
  VkCompositeAlphaFlagBitsKHR composite_alpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (uint32_t i = 0; i < 4; i++) {
    if (surf_caps.supportedCompositeAlpha & composite_alpha_flags[i]) {
      composite_alpha = composite_alpha_flags[i];
      break;
    }
  }

  VkSwapchainCreateInfoKHR create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface;
  // On Android, vkGetSwapchainImagesKHR is always returning 1 more image than
  // our min image count
#ifdef __ANDROID__
  create_info.minImageCount = image_count - 1;
#else
  create_info.minImageCount = image_count;
#endif
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = swapchain_extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  create_info.compositeAlpha = composite_alpha;
  create_info.preTransform = pre_transform;
  create_info.presentMode = present_mode;
  create_info.surface = surface;
  create_info.oldSwapchain = *swapchain;

  err = vkCreateSwapchainKHR(device, &create_info, vk_alloc, swapchain);
  assert(err == VK_SUCCESS);

  swap_info = (SwapchainInfo){
      .valid = true,
      .format = surface_format.format,
      .color_space = surface_format.colorSpace,
      .present_mode = present_mode,
      .image_count = image_count,
      .width = swapchain_extent.width,
      .height = swapchain_extent.height,
  };

  hb_free(tmp_alloc, surface_formats);

  return swap_info;
}

static bool demo_init_image_views(Demo *d) {
  VkResult err = VK_SUCCESS;
  // Get Swapchain Images
  {
    uint32_t img_count = 0;
    err = vkGetSwapchainImagesKHR(d->device, d->swapchain, &img_count, NULL);
    if (err != VK_SUCCESS) {
      assert(false);
      return false;
    }

    // Device may really want us to have called vkGetSwapchainImagesKHR
    // For now just assert that making that call doesn't change our desired
    // swapchain images
    if (d->swap_info.image_count != img_count) {
      assert(false);
      return false;
    }

    err =
        vkGetSwapchainImagesKHR(d->device, d->swapchain,
                                &d->swap_info.image_count, d->swapchain_images);
    if (err != VK_SUCCESS && err == VK_INCOMPLETE) {
      assert(false);
      return false;
    }
  }

  // Create Image Views
  {
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      if (d->depth_buffer_views[i]) {
        vkDestroyImageView(d->device, d->swapchain_image_views[i], d->vk_alloc);
      }
    }

    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = d->swap_info.format;
    create_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
    };
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
    };

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      create_info.image = d->swapchain_images[i];
      err = vkCreateImageView(d->device, &create_info, d->vk_alloc,
                              &d->swapchain_image_views[i]);
      if (err != VK_SUCCESS) {
        assert(false);
        return false;
      }
    }
  }

  // Create Depth Buffers
  {
    if (d->depth_buffers.image != VK_NULL_HANDLE) {
      destroy_gpuimage(d->vma_alloc, &d->depth_buffers);
    }

    VkImageCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    create_info.extent =
        (VkExtent3D){d->swap_info.width, d->swap_info.height, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = FRAME_LATENCY;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pUserData = (void *)("Depth Buffer Memory");
    err = create_gpuimage(d->vma_alloc, &create_info, &alloc_info,
                          &d->depth_buffers);
    if (err != VK_SUCCESS) {
      assert(false);
      return false;
    }
  }

  // Create Depth Buffer Views
  {
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      if (d->depth_buffer_views[i]) {
        vkDestroyImageView(d->device, d->depth_buffer_views[i], d->vk_alloc);
      }
    }

    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = d->depth_buffers.image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    create_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
    };
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1,
    };

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      create_info.subresourceRange.baseArrayLayer = i;
      err = vkCreateImageView(d->device, &create_info, d->vk_alloc,
                              &d->depth_buffer_views[i]);
      if (err != VK_SUCCESS) {
        assert(false);
        return false;
      }
    }
  }

  return true;
}

static bool demo_init_framebuffers(Demo *d) {
  VkResult err = VK_SUCCESS;

  // Cleanup previous framebuffers
  {
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      if (d->main_pass_framebuffers[i]) {
        vkDestroyFramebuffer(d->device, d->main_pass_framebuffers[i],
                             d->vk_alloc);
      }
    }
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      if (d->main_pass_framebuffers[i]) {
        vkDestroyFramebuffer(d->device, d->ui_pass_framebuffers[i],
                             d->vk_alloc);
      }
    }
  }

  VkFramebufferCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  create_info.renderPass = d->render_pass;
  create_info.attachmentCount = 2;
  create_info.width = d->swap_info.width;
  create_info.height = d->swap_info.height;
  create_info.layers = 1;

  // Create main pass framebuffers
  for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
    VkImageView attachments[2] = {
        d->swapchain_image_views[i],
        d->depth_buffer_views[i],
    };

    create_info.pAttachments = attachments;
    err = vkCreateFramebuffer(d->device, &create_info, d->vk_alloc,
                              &d->main_pass_framebuffers[i]);
    if (err != VK_SUCCESS) {
      assert(false);
      return false;
    }
  }

  // Create ui pass framebuffers
  for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
    VkImageView attachments[1] = {
        d->swapchain_image_views[i],
    };

    create_info.attachmentCount = 1;
    create_info.pAttachments = attachments;
    create_info.renderPass = d->imgui_pass;
    err = vkCreateFramebuffer(d->device, &create_info, d->vk_alloc,
                              &d->ui_pass_framebuffers[i]);
    if (err != VK_SUCCESS) {
      assert(false);
      return false;
    }
  }

  return true;
}

static bool demo_init_imgui(Demo *d, SDL_Window *window) {
  (void)window;
  ImGuiContext *ctx = igCreateContext(NULL);
  ImGuiIO *io = igGetIO();

  uint8_t *pixels = NULL;
  int32_t tex_w = 0;
  int32_t tex_h = 0;
  int32_t bytes_pp = 0;
  ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &tex_w, &tex_h, &bytes_pp);

  size_t size = tex_w * tex_h * bytes_pp;

  // Create and upload imgui atlas texture
  GPUTexture imgui_atlas = {0};
  {
    // Describe cpu-side texture
    TextureMip mip = {
        .width = tex_w,
        .height = tex_h,
        .depth = 1,
        .data = pixels,
    };
    TextureLayer layer = {
        .width = tex_w,
        .height = tex_h,
        .depth = 1,
        .mips = &mip,
    };
    CPUTexture cpu_atlas = {
        .data = pixels,
        .data_size = size,
        .layer_count = 1,
        .layers = &layer,
        .mip_count = 1,
    };

    VkResult err = (VkResult)create_texture(
        d->device, d->vma_alloc, d->vk_alloc, &cpu_atlas, d->upload_mem_pool,
        d->texture_mem_pool, &imgui_atlas, false);
    assert(err == VK_SUCCESS);
    (void)err;

    demo_upload_texture(d, &imgui_atlas);
  }

  // Setup interaction with SDL
  {
    io->BackendPlatformName = "HB SDL Test";
    io->BackendRendererName = "HB Vulkan Renderer";
    io->BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io->BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    // Keyboard mapping.Dear ImGui will use those indices to peek into the
    // io->KeysDown[] array.
    io->KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
    io->KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
    io->KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
    io->KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
    io->KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
    io->KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
    io->KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
    io->KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
    io->KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
    io->KeyMap[ImGuiKey_Insert] = SDL_SCANCODE_INSERT;
    io->KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
    io->KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
    io->KeyMap[ImGuiKey_Space] = SDL_SCANCODE_SPACE;
    io->KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
    io->KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
    io->KeyMap[ImGuiKey_KeyPadEnter] = SDL_SCANCODE_KP_ENTER;
    io->KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
    io->KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
    io->KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
    io->KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
    io->KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
    io->KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

    // io->SetClipboardTextFn = ImGui_ImplSDL2_SetClipboardText;
    // io->GetClipboardTextFn = ImGui_ImplSDL2_GetClipboardText;
    io->ClipboardUserData = NULL;

    // Could load mouse cursors here
  }

  d->imgui_atlas = imgui_atlas;
  d->ig_ctx = ctx;
  d->ig_io = io;

  return true;
}

bool demo_init(SDL_Window *window, VkInstance instance, Allocator std_alloc,
               Allocator tmp_alloc, const VkAllocationCallbacks *vk_alloc,
               Demo *d) {
  TracyCZoneN(ctx, "demo_init", true);
  VkResult err = VK_SUCCESS;

  // Get the GPU we want to run on
  VkPhysicalDevice gpu = select_gpu(instance, tmp_alloc);
  if (gpu == VK_NULL_HANDLE) {
    return false;
  }

  // Check physical device properties
  VkPhysicalDeviceProperties gpu_props = {0};
  vkGetPhysicalDeviceProperties(gpu, &gpu_props);

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count, NULL);

  VkQueueFamilyProperties *queue_props =
      hb_alloc_nm_tp(std_alloc, queue_family_count, VkQueueFamilyProperties);
  assert(queue_props);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count,
                                           queue_props);

  VkPhysicalDeviceFeatures gpu_features = {0};
  vkGetPhysicalDeviceFeatures(gpu, &gpu_features);

  VkPhysicalDeviceMemoryProperties gpu_mem_props;
  vkGetPhysicalDeviceMemoryProperties(gpu, &gpu_mem_props);

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
    assert(false);
    return false;
  }

  uint32_t graphics_queue_family_index = UINT32_MAX;
  uint32_t present_queue_family_index = UINT32_MAX;
  {
    // Iterate over each queue to learn whether it supports presenting:
    VkBool32 *supports_present =
        hb_alloc_nm_tp(tmp_alloc, queue_family_count, VkBool32);
    for (uint32_t i = 0; i < queue_family_count; i++) {
      vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface,
                                           &supports_present[i]);
    }

    // Search for a graphics and a present queue in the array of queue
    // families, try to find one that supports both
    for (uint32_t i = 0; i < queue_family_count; i++) {
      if ((queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        if (graphics_queue_family_index == UINT32_MAX) {
          graphics_queue_family_index = i;
        }

        if (supports_present[i] == VK_TRUE) {
          graphics_queue_family_index = i;
          present_queue_family_index = i;
          break;
        }
      }
    }

    if (present_queue_family_index == UINT32_MAX) {
      // If didn't find a queue that supports both graphics and present, then
      // find a separate present queue.
      for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (supports_present[i] == VK_TRUE) {
          present_queue_family_index = i;
          break;
        }
      }
    }
    hb_free(tmp_alloc, supports_present);

    // Generate error if could not find both a graphics and a present queue
    if (graphics_queue_family_index == UINT32_MAX ||
        present_queue_family_index == UINT32_MAX) {
      return false;
    }
  }

  // Create Logical Device
  uint32_t device_ext_count = 0;
  const char *device_ext_names[MAX_EXT_COUNT] = {0};

  // Need a swapchain
  {
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  }

#ifdef TRACY_ENABLE
#ifndef __ANDROID__
  // Enable calibrated timestamps
  {
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
  }
#endif
#endif

  // TODO: Check for Raytracing Support
  /*
  {
    // Required for Spirv 1.4
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME;

    // Required for VK_KHR_ray_tracing_pipeline
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;

    // Required for VK_KHR_acceleration_structure
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;

    // Required for raytracing
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
  }
  */

  VkDevice device = create_device(gpu, graphics_queue_family_index,
                                  present_queue_family_index, device_ext_count,
                                  vk_alloc, device_ext_names);

  VkQueue graphics_queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, graphics_queue_family_index, 0, &graphics_queue);

  VkQueue present_queue = VK_NULL_HANDLE;
  if (graphics_queue_family_index == present_queue_family_index) {
    present_queue = graphics_queue;
  } else {
    vkGetDeviceQueue(device, present_queue_family_index, 0, &present_queue);
  }

  // Create Allocator
  VmaAllocator vma_alloc = {0};
  {
    VmaVulkanFunctions volk_functions = {0};
    volk_functions.vkGetPhysicalDeviceProperties =
        vkGetPhysicalDeviceProperties;
    volk_functions.vkGetPhysicalDeviceMemoryProperties =
        vkGetPhysicalDeviceMemoryProperties;
    volk_functions.vkAllocateMemory = vkAllocateMemory;
    volk_functions.vkFreeMemory = vkFreeMemory;
    volk_functions.vkMapMemory = vkMapMemory;
    volk_functions.vkUnmapMemory = vkUnmapMemory;
    volk_functions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    volk_functions.vkInvalidateMappedMemoryRanges =
        vkInvalidateMappedMemoryRanges;
    volk_functions.vkBindBufferMemory = vkBindBufferMemory;
    volk_functions.vkBindImageMemory = vkBindImageMemory;
    volk_functions.vkGetBufferMemoryRequirements =
        vkGetBufferMemoryRequirements;
    volk_functions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    volk_functions.vkCreateBuffer = vkCreateBuffer;
    volk_functions.vkDestroyBuffer = vkDestroyBuffer;
    volk_functions.vkCreateImage = vkCreateImage;
    volk_functions.vkDestroyImage = vkDestroyImage;
    volk_functions.vkCmdCopyBuffer = vkCmdCopyBuffer;

    VmaDeviceMemoryCallbacks vma_callbacks = {
        vma_alloc_fn,
        vma_free_fn,
        NULL,
    };

    VmaAllocatorCreateInfo create_info = {0};
    create_info.physicalDevice = gpu;
    create_info.device = device;
    create_info.pVulkanFunctions = &volk_functions;
    create_info.instance = instance;
    create_info.vulkanApiVersion = VK_API_VERSION_1_0;
    create_info.pAllocationCallbacks = vk_alloc;
    create_info.pDeviceMemoryCallbacks = &vma_callbacks;
    err = vmaCreateAllocator(&create_info, &vma_alloc);
    assert(err == VK_SUCCESS);
  }

  uint32_t width = 0;
  uint32_t height = 0;
  {
    int32_t w = 0;
    int32_t h = 0;
    SDL_GetWindowSize(window, &w, &h);
    width = (uint32_t)w;
    height = (uint32_t)h;
  }

  // Create Swapchain
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  SwapchainInfo swap_info = init_swapchain(window, device, gpu, surface,
                                           &swapchain, vk_alloc, tmp_alloc);

  // Create Render Pass
  VkRenderPass render_pass = VK_NULL_HANDLE;
  {
    VkAttachmentDescription color_attachment = {0};
    color_attachment.format = swap_info.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {0};
    depth_attachment.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attachments[2] = {color_attachment,
                                              depth_attachment};

    VkAttachmentReference color_attachment_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_attachment_ref = {
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference attachment_refs[1] = {color_attachment_ref};

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = attachment_refs;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency subpass_dep = {0};
    subpass_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 2;
    create_info.pAttachments = attachments;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.pDependencies = &subpass_dep;
    err = vkCreateRenderPass(device, &create_info, vk_alloc, &render_pass);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)render_pass, VK_OBJECT_TYPE_RENDER_PASS,
                "main render pass");
  }

  // Create ImGui Render Pass
  VkRenderPass imgui_pass = VK_NULL_HANDLE;
  {
    VkAttachmentDescription color_attachment = {0};
    color_attachment.format = swap_info.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription attachments[1] = {color_attachment};

    VkAttachmentReference color_attachment_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference attachment_refs[1] = {color_attachment_ref};

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = attachment_refs;

    VkSubpassDependency subpass_dep = {0};
    subpass_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = attachments;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.pDependencies = &subpass_dep;
    err = vkCreateRenderPass(device, &create_info, vk_alloc, &imgui_pass);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)imgui_pass, VK_OBJECT_TYPE_RENDER_PASS,
                "imgui render pass");
  }

  // Create Pipeline Cache
  VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
  {
    TracyCZoneN(pipe_cache_ctx, "init pipeline cache", true);
    size_t data_size = 0;
    void *data = NULL;

    // If an existing pipeline cache exists, load it
    SDL_RWops *cache_file = SDL_RWFromFile("./pipeline.cache", "rb");
    if (cache_file != NULL) {
      data_size = (size_t)SDL_RWsize(cache_file);

      data = hb_alloc(std_alloc, data_size);

      SDL_RWread(cache_file, data, data_size, 1);

      SDL_RWclose(cache_file);
    }

    VkPipelineCacheCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    create_info.initialDataSize = data_size;
    create_info.pInitialData = data;
    err =
        vkCreatePipelineCache(device, &create_info, vk_alloc, &pipeline_cache);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)pipeline_cache, VK_OBJECT_TYPE_PIPELINE_CACHE,
                "pipeline cache");

    if (data) {
      hb_free(std_alloc, data);
    }
    TracyCZoneEnd(pipe_cache_ctx);
  }

  VkPushConstantRange sky_const_range = {
      VK_SHADER_STAGE_ALL_GRAPHICS,
      0,
      sizeof(SkyPushConstants),
  };

  VkPushConstantRange imgui_const_range = {
      VK_SHADER_STAGE_ALL_GRAPHICS,
      0,
      sizeof(ImGuiPushConstants),
  };

  // Create Immutable Sampler
  VkSampler sampler = VK_NULL_HANDLE;
  {
    VkSamplerCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create_info.magFilter = VK_FILTER_LINEAR;
    create_info.minFilter = VK_FILTER_LINEAR;
    create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create_info.anisotropyEnable = VK_FALSE;
    create_info.maxAnisotropy = 1.0f;
    create_info.maxLod = 14.0f; // Hack; known number of mips for 8k textures
    create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    err = vkCreateSampler(device, &create_info, vk_alloc, &sampler);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)sampler, VK_OBJECT_TYPE_SAMPLER,
                "immutable sampler");
  }

  // Create Common Object DescriptorSet Layout
  VkDescriptorSetLayout gltf_object_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[1] = {
        {
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_VERTEX_BIT,
            NULL,
        },
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 1;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_object_set_layout);
    assert(err == VK_SUCCESS);
    set_vk_name(device, (uint64_t)gltf_object_set_layout,
                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "gltf object set layout");
  }

  // Create Common Per-View DescriptorSet Layout
  VkDescriptorSetLayout gltf_view_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            NULL,
        },
        {
            1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            NULL,
        },
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 2;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_view_set_layout);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)gltf_view_set_layout,
                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "gltf view set layout");
  }

  // Create GLTF Descriptor Set Layout
  VkDescriptorSetLayout gltf_material_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[5] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 5;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_material_set_layout);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)gltf_material_set_layout,
                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                "gltf material set layout");
  }

  // Create GLTF Pipeline Layout
  VkPipelineLayout gltf_pipe_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayout layouts[] = {
        gltf_material_set_layout,
        gltf_object_set_layout,
        gltf_view_set_layout,
    };
    const uint32_t layout_count =
        sizeof(layouts) / sizeof(VkDescriptorSetLayout);

    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = layout_count;
    create_info.pSetLayouts = layouts;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &gltf_pipe_layout);
    set_vk_name(device, (uint64_t)gltf_pipe_layout,
                VK_OBJECT_TYPE_PIPELINE_LAYOUT, "gltf pipeline layout");
    assert(err == VK_SUCCESS);
  }

  // Create GLTF Pipeline
  GPUPipeline *gltf_pipeline = NULL;
  err = create_gltf_pipeline(device, vk_alloc, tmp_alloc, std_alloc,
                             pipeline_cache, render_pass, width, height,
                             gltf_pipe_layout, &gltf_pipeline);
  assert(err == VK_SUCCESS);

  // Create GLTF RT Pipeline Layout
  // Create GLTF Descriptor Set Layout
  VkDescriptorSetLayout gltf_rt_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[3] = {
        {1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR,
         NULL},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR, NULL},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 3;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_rt_layout);
    assert(err == VK_SUCCESS);
  }

  VkPipelineLayout gltf_rt_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &gltf_rt_layout;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &gltf_rt_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create Skydome Descriptor Set Layout
  VkDescriptorSetLayout skydome_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[1] = {
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 1;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &skydome_set_layout);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)skydome_set_layout,
                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "skydome set layout");
  }

  // Create Descriptor Set for Hosek coeff data
  VkDescriptorSetLayout hosek_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[1] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 1;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &hosek_set_layout);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)hosek_set_layout,
                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "hosek set layout");
  }

  // Create Skydome Pipeline Layout
  VkPipelineLayout skydome_pipe_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayout layouts[] = {
        skydome_set_layout,
        hosek_set_layout,
    };

    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 2;
    create_info.pSetLayouts = layouts;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &sky_const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &skydome_pipe_layout);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)skydome_pipe_layout,
                VK_OBJECT_TYPE_PIPELINE_LAYOUT, "skydome pipeline layout");
  }

  // Create Skydome Pipeline
  VkPipeline skydome_pipeline = VK_NULL_HANDLE;
  err = create_skydome_pipeline(device, vk_alloc, pipeline_cache, render_pass,
                                width, height, skydome_pipe_layout,
                                &skydome_pipeline);
  assert(err == VK_SUCCESS);

  // HACK: Get this function here...
  // PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR =
  //    (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
  //        device, "vkCreateRayTracingPipelinesKHR");

  // Create GLTF Ray Tracing Pipeline
  // gpupipeline *gltf_rt_pipeline = NULL;
  // err = create_gltf_rt_pipeline(
  //    device, vk_alloc, tmp_alloc, std_alloc, pipeline_cache,
  //    vkCreateRayTracingPipelinesKHR, render_pass, width, height,
  //    gltf_rt_pipe_layout, &gltf_rt_pipeline);
  // assert(err == VK_SUCCESS);

  // Create ImGui Descriptor Set Layout
  VkDescriptorSetLayout imgui_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 2;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &imgui_set_layout);
    assert(err == VK_SUCCESS);
  }

  // Create ImGui Pipeline Layout
  VkPipelineLayout imgui_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &imgui_set_layout;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &imgui_const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &imgui_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create ImGui pipeline
  VkPipeline imgui_pipeline = VK_NULL_HANDLE;
  err =
      create_imgui_pipeline(device, vk_alloc, pipeline_cache, imgui_pass, width,
                            height, imgui_pipe_layout, &imgui_pipeline);
  assert(err == VK_SUCCESS);

  // Create a pool for host memory uploads
  VmaPool upload_mem_pool = VK_NULL_HANDLE;
  {
    TracyCZoneN(vma_pool_ctx, "init vma upload pool", true);
    uint32_t mem_type_idx = 0xFFFFFFFF;
    // Find the desired memory type index
    for (uint32_t i = 0; i < gpu_mem_props.memoryTypeCount; ++i) {
      VkMemoryType type = gpu_mem_props.memoryTypes[i];
      if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        mem_type_idx = i;
        break;
      }
    }
    assert(mem_type_idx != 0xFFFFFFFF);

    VmaPoolCreateInfo create_info = {0};
    create_info.memoryTypeIndex = mem_type_idx;
    err = vmaCreatePool(vma_alloc, &create_info, &upload_mem_pool);
    assert(err == VK_SUCCESS);

    TracyCZoneEnd(vma_pool_ctx);
  }

  // Create a pool for texture memory
  VmaPool texture_mem_pool = VK_NULL_HANDLE;
  {
    TracyCZoneN(vma_pool_e, "init vma texture pool", true);
    uint32_t mem_type_idx = 0xFFFFFFFF;
    // Find the desired memory type index
    for (uint32_t i = 0; i < gpu_mem_props.memoryTypeCount; ++i) {
      VkMemoryType type = gpu_mem_props.memoryTypes[i];
      if (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        mem_type_idx = i;
        break;
      }
    }
    assert(mem_type_idx != 0xFFFFFFFF);

    // block size to fit a 4k R8G8B8A8 uncompressed texture
    uint64_t block_size = (uint64_t)(4096.0 * 4096.0 * 4.0);

    VmaPoolCreateInfo create_info = {0};
    create_info.memoryTypeIndex = mem_type_idx;
    create_info.blockSize = block_size;
    create_info.minBlockCount = 10;
    err = vmaCreatePool(vma_alloc, &create_info, &texture_mem_pool);
    assert(err == VK_SUCCESS);
    TracyCZoneEnd(vma_pool_e);
  }

  // Create Skydome Mesh
  GPUMesh skydome = {0};
  {
    CPUMesh *skydome_cpu = create_skydome(&tmp_alloc);

    err = create_gpumesh(vma_alloc, skydome_cpu, &skydome);
    assert(err == VK_SUCCESS);
  }

  // Create Uniform buffer for sky data
  GPUConstBuffer sky_const_buffer =
      create_gpuconstbuffer(device, vma_alloc, vk_alloc, sizeof(SkyData));

  // Create Storage buffer for hosek data
  GPUConstBuffer hosek_const_buffer = create_gpustoragebuffer(
      device, vma_alloc, vk_alloc, sizeof(SkyHosekData));

  // Create Uniform buffer for camera data
  GPUConstBuffer camera_const_buffer = create_gpuconstbuffer(
      device, vma_alloc, vk_alloc, sizeof(CommonCameraData));

  // Create Uniform buffer for light data
  GPUConstBuffer light_const_buffer = create_gpuconstbuffer(
      device, vma_alloc, vk_alloc, sizeof(CommonLightData));

  // Composite main scene
  Scene *main_scene = NULL;
  {
    main_scene = hb_alloc_tp(std_alloc, Scene);
    if (!main_scene) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", "Failed to alloc main scene");
      SDL_TriggerBreakpoint();
      return false;
    }
    DemoAllocContext ctx = {
        .device = device,
        .std_alloc = std_alloc,
        .tmp_alloc = tmp_alloc,
        .vk_alloc = vk_alloc,
        .vma_alloc = vma_alloc,
        .up_pool = upload_mem_pool,
        .tex_pool = texture_mem_pool,
    };
    if (create_scene(ctx, main_scene) != 0) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", "Failed to load main scene");
      SDL_TriggerBreakpoint();
      return false;
    }

    if (scene_append_gltf(main_scene, ASSET_PREFIX "scenes/Floor.glb") != 0) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to append floor to main scene");
      SDL_TriggerBreakpoint();
      return false;
    }

    if (scene_append_gltf(main_scene, ASSET_PREFIX "scenes/duck.glb") != 0) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to append duck to main scene");
      SDL_TriggerBreakpoint();
      return false;
    }
  }

  // Create resources for screenshots
  GPUImage screenshot_image = {0};
  {
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_GPU_TO_CPU,
        .pool = upload_mem_pool,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    err =
        create_gpuimage(vma_alloc, &image_info, &alloc_info, &screenshot_image);
    assert(err == VK_SUCCESS);
  }

  VkFence screenshot_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    err = vkCreateFence(device, &create_info, vk_alloc, &screenshot_fence);
    assert(err == VK_SUCCESS);
  }

  // Apply to output var
  d->tmp_alloc = tmp_alloc;
  d->std_alloc = std_alloc;
  d->window = window;
  d->vk_alloc = vk_alloc;
  d->instance = instance;
  d->gpu = gpu;
  d->vma_alloc = vma_alloc;
  d->gpu_props = gpu_props;
  d->gpu_mem_props = gpu_mem_props;
  d->queue_family_count = queue_family_count;
  d->queue_props = queue_props;
  d->gpu_features = gpu_features;
  d->surface = surface;
  d->graphics_queue_family_index = graphics_queue_family_index;
  d->present_queue_family_index = present_queue_family_index;
  d->separate_present_queue =
      (graphics_queue_family_index != present_queue_family_index);
  d->device = device;
  d->present_queue = present_queue;
  d->graphics_queue = graphics_queue;
  d->swap_info = swap_info;
  d->swapchain = swapchain;
  d->render_pass = render_pass;
  d->imgui_pass = imgui_pass;
  d->pipeline_cache = pipeline_cache;
  d->sampler = sampler;
  d->skydome_layout = skydome_set_layout;
  d->hosek_layout = hosek_set_layout;
  d->skydome_pipe_layout = skydome_pipe_layout;
  d->skydome_pipeline = skydome_pipeline;
  d->sky_const_buffer = sky_const_buffer;
  d->hosek_const_buffer = hosek_const_buffer;
  d->camera_const_buffer = camera_const_buffer;
  d->light_const_buffer = light_const_buffer;
  d->gltf_material_set_layout = gltf_material_set_layout;
  d->gltf_object_set_layout = gltf_object_set_layout;
  d->gltf_view_set_layout = gltf_view_set_layout;
  d->gltf_pipe_layout = gltf_pipe_layout;
  d->gltf_pipeline = gltf_pipeline;
  d->gltf_rt_layout = gltf_rt_layout;
  d->gltf_rt_pipe_layout = gltf_rt_pipe_layout;
  // d->gltf_rt_pipeline = gltf_rt_pipeline;
  d->imgui_layout = imgui_set_layout;
  d->imgui_pipe_layout = imgui_pipe_layout;
  d->imgui_pipeline = imgui_pipeline;
  d->upload_mem_pool = upload_mem_pool;
  d->texture_mem_pool = texture_mem_pool;
  d->skydome_gpu = skydome;
  d->main_scene = main_scene;
  d->screenshot_image = screenshot_image;
  d->screenshot_fence = screenshot_fence;
  d->frame_idx = 0;

  // Setup data for hosek buffer
  {
    TracyCZoneN(hosek_ctx, "Update Hosek Data", true);

    VmaAllocation hosek_host_alloc = d->hosek_const_buffer.host.alloc;

    uint8_t *data = NULL;
    err = vmaMapMemory(vma_alloc, hosek_host_alloc, (void **)&data);
    if (err != VK_SUCCESS) {
      assert(0);
      return false;
    }
    SkyHosekData hosek_data = {0};
    init_hosek_data(&hosek_data);

    SDL_memcpy(data, &hosek_data, sizeof(SkyHosekData));
    vmaUnmapMemory(vma_alloc, hosek_host_alloc);

    demo_upload_const_buffer(d, &d->hosek_const_buffer);
    TracyCZoneEnd(hosek_ctx);
  }

  demo_upload_mesh(d, &d->skydome_gpu);
  demo_upload_scene(d, d->main_scene);

  // Create Semaphores
  {
    VkSemaphoreCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->upload_complete_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->img_acquired_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->swapchain_image_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->render_complete_sems[i]);
      assert(err == VK_SUCCESS);
    }
  }

  if (!demo_init_image_views(d)) {
    assert(false);
    return false;
  }

  if (!demo_init_framebuffers(d)) {
    assert(false);
    return false;
  }

  // Create Command Pools
  {
    VkCommandPoolCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.queueFamilyIndex = graphics_queue_family_index;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateCommandPool(device, &create_info, vk_alloc,
                                &d->command_pools[i]);
      set_vk_name(device, (uint64_t)d->command_pools[i],
                  VK_OBJECT_TYPE_COMMAND_POOL, "command pool");
      assert(err == VK_SUCCESS);
    }
  }

  // Allocate Command Buffers
  {
    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.commandPool = d->command_pools[i];
      err = vkAllocateCommandBuffers(device, &alloc_info,
                                     &d->graphics_buffers[i]);
      assert(err == VK_SUCCESS);
      err =
          vkAllocateCommandBuffers(device, &alloc_info, &d->upload_buffers[i]);
      assert(err == VK_SUCCESS);
      err = vkAllocateCommandBuffers(device, &alloc_info,
                                     &d->screenshot_buffers[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create profiling contexts
  for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
    d->tracy_gpu_contexts[i] = TracyCVkContextExt(
        d->gpu, d->device, d->graphics_queue, d->graphics_buffers[i],
        vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
        vkGetCalibratedTimestampsEXT);
  }

  // Create Descriptor Set Pools
  {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}};
    const uint32_t pool_sizes_count =
        sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);

    VkDescriptorPoolCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.maxSets = 7;
    create_info.poolSizeCount = pool_sizes_count;
    create_info.pPoolSizes = pool_sizes;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateDescriptorPool(device, &create_info, vk_alloc,
                                   &d->descriptor_pools[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Descriptor Sets
  {
    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorSetCount = 1;

    alloc_info.pSetLayouts = &skydome_set_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->skydome_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    // Only need one descriptor set for the hosek data
    alloc_info.pSetLayouts = &hosek_set_layout;
    {
      alloc_info.descriptorPool = d->descriptor_pools[0];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->hosek_descriptor_set);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &gltf_view_set_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->gltf_view_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &imgui_set_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->imgui_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Must do this before descriptor set writes so we can be sure to create the
  // imgui resources on time
  if (!demo_init_imgui(d, window)) {
    TracyCZoneEnd(ctx);
    return false;
  }

  // Write textures to descriptor set
  {
    VkDescriptorBufferInfo skydome_info = {sky_const_buffer.gpu.buffer, 0,
                                           sky_const_buffer.size};
    VkDescriptorImageInfo imgui_info = {
        NULL, d->imgui_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo camera_info = {camera_const_buffer.gpu.buffer, 0,
                                          camera_const_buffer.size};
    VkDescriptorBufferInfo light_info = {light_const_buffer.gpu.buffer, 0,
                                         light_const_buffer.size};
    VkWriteDescriptorSet writes[4] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &skydome_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &imgui_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &camera_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &light_info,
        },
    };
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      VkDescriptorSet gltf_view_set = d->gltf_view_descriptor_sets[i];
      VkDescriptorSet skydome_set = d->skydome_descriptor_sets[i];
      VkDescriptorSet imgui_set = d->imgui_descriptor_sets[i];

      writes[0].dstSet = skydome_set;
      writes[1].dstSet = imgui_set;

      writes[2].dstSet = gltf_view_set;
      writes[3].dstSet = gltf_view_set;

      vkUpdateDescriptorSets(device, 4, writes, 0, NULL);
    }
  }

  // Write Hosek descriptor set seperately
  {
    VkDescriptorBufferInfo hosek_info = {hosek_const_buffer.gpu.buffer, 0,
                                         hosek_const_buffer.size};
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &hosek_info,
        .dstSet = d->hosek_descriptor_set,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
  }

  // Create Fences
  {
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateFence(device, &create_info, vk_alloc, &d->fences[i]);
      assert(err == VK_SUCCESS);
    }
  }

  TracyCZoneEnd(ctx);

  return true;
}

void demo_destroy(Demo *d) {
  TracyCZoneN(ctx, "demo_destroy", true);

  VkDevice device = d->device;
  VmaAllocator vma_alloc = d->vma_alloc;
  const VkAllocationCallbacks *vk_alloc = d->vk_alloc;

  vkDeviceWaitIdle(device);

  // Write out the pipeline cache
  {
    VkResult err = VK_SUCCESS;

    size_t cache_size = 0;
    err = vkGetPipelineCacheData(device, d->pipeline_cache, &cache_size, NULL);
    if (err == VK_SUCCESS) {
      void *cache = hb_alloc(d->tmp_alloc, cache_size);
      err =
          vkGetPipelineCacheData(device, d->pipeline_cache, &cache_size, cache);
      if (err == VK_SUCCESS) {

        SDL_RWops *cache_file = SDL_RWFromFile("./pipeline.cache", "wb");
        if (cache_file != NULL) {
          SDL_RWwrite(cache_file, cache, cache_size, 1);
          SDL_RWclose(cache_file);
        }
      }
    }
  }

  for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
    TracyCVkContextDestroy(d->tracy_gpu_contexts[i]);

    vkDestroyImageView(device, d->depth_buffer_views[i], vk_alloc);
    vkDestroyDescriptorPool(device, d->descriptor_pools[i], vk_alloc);
    vkDestroyFence(device, d->fences[i], vk_alloc);
    vkDestroySemaphore(device, d->upload_complete_sems[i], vk_alloc);
    vkDestroySemaphore(device, d->render_complete_sems[i], vk_alloc);
    vkDestroySemaphore(device, d->swapchain_image_sems[i], vk_alloc);
    vkDestroySemaphore(device, d->img_acquired_sems[i], vk_alloc);
    vkDestroyImageView(device, d->swapchain_image_views[i], vk_alloc);
    vkDestroyFramebuffer(device, d->main_pass_framebuffers[i], vk_alloc);
    vkDestroyFramebuffer(device, d->ui_pass_framebuffers[i], vk_alloc);
    vkDestroyCommandPool(device, d->command_pools[i], vk_alloc);

    destroy_gpumesh(vma_alloc, &d->imgui_gpu[i]);
  }

  destroy_gpuimage(vma_alloc, &d->depth_buffers);

  hb_free(d->std_alloc, d->imgui_mesh_data);

  destroy_scene(d->main_scene);
  hb_free(d->std_alloc, d->main_scene);

  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->hosek_const_buffer);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->sky_const_buffer);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->camera_const_buffer);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->light_const_buffer);

  // Clean-up object const buffer pool
  for (uint32_t i = 0; i < d->obj_const_buffer_block_count; ++i) {
    GPUConstBuffer *block = d->obj_const_buffer_blocks[i];
    for (uint32_t ii = 0; ii < CONST_BUFFER_BLOCK_SIZE; ++ii) {
      destroy_gpuconstbuffer(d->device, d->vma_alloc, d->vk_alloc, block[ii]);
    }
    hb_free(d->std_alloc, block);
  }
  hb_free(d->std_alloc, d->obj_const_buffer_blocks);

  destroy_gpumesh(vma_alloc, &d->skydome_gpu);
  destroy_texture(device, vma_alloc, vk_alloc, &d->imgui_atlas);

  vkDestroyFence(device, d->screenshot_fence, vk_alloc);
  destroy_gpuimage(vma_alloc, &d->screenshot_image);

  vmaDestroyPool(vma_alloc, d->upload_mem_pool);
  vmaDestroyPool(vma_alloc, d->texture_mem_pool);

  hb_free(d->std_alloc, d->queue_props);
  vkDestroySampler(device, d->sampler, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->hosek_layout, vk_alloc);
  vkDestroyDescriptorSetLayout(device, d->skydome_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->skydome_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->skydome_pipeline, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->gltf_rt_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->gltf_rt_pipe_layout, vk_alloc);
  // destroy_gpupipeline(device, vk_alloc, d->gltf_rt_pipeline);

  vkDestroyDescriptorSetLayout(device, d->gltf_material_set_layout, vk_alloc);
  vkDestroyDescriptorSetLayout(device, d->gltf_object_set_layout, vk_alloc);
  vkDestroyDescriptorSetLayout(device, d->gltf_view_set_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->gltf_pipe_layout, vk_alloc);
  destroy_gpupipeline(device, d->std_alloc, vk_alloc, d->gltf_pipeline);

  vkDestroyDescriptorSetLayout(device, d->imgui_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->imgui_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->imgui_pipeline, vk_alloc);

  vkDestroyPipelineCache(device, d->pipeline_cache, vk_alloc);
  vkDestroyRenderPass(device, d->render_pass, vk_alloc);
  vkDestroyRenderPass(device, d->imgui_pass, vk_alloc);
  vkDestroySwapchainKHR(device, d->swapchain, vk_alloc);
  vkDestroySurfaceKHR(d->instance, d->surface,
                      NULL); // Surface is created by SDL
  vmaDestroyAllocator(vma_alloc);
  vkDestroyDevice(device, vk_alloc);
  *d = (Demo){0};

  igDestroyContext(d->ig_ctx);

  TracyCZoneEnd(ctx);
}

void demo_upload_const_buffer(Demo *d, const GPUConstBuffer *buffer) {
  uint32_t buffer_idx = d->const_buffer_upload_count;
  assert(d->const_buffer_upload_count + 1 < CONST_BUFFER_UPLOAD_QUEUE_SIZE);
  d->const_buffer_upload_queue[buffer_idx] = *buffer;
  d->const_buffer_upload_count++;
}

void demo_upload_mesh(Demo *d, const GPUMesh *mesh) {
  uint32_t mesh_idx = d->mesh_upload_count;
  assert(d->mesh_upload_count + 1 < MESH_UPLOAD_QUEUE_SIZE);
  d->mesh_upload_queue[mesh_idx] = *mesh;
  d->mesh_upload_count++;
}

void demo_upload_texture(Demo *d, const GPUTexture *tex) {
  uint32_t tex_idx = d->texture_upload_count;
  assert(d->texture_upload_count + 1 < TEXTURE_UPLOAD_QUEUE_SIZE);
  d->texture_upload_queue[tex_idx] = *tex;
  d->texture_upload_count++;
}

void demo_upload_scene(Demo *d, const Scene *s) {
  for (uint32_t i = 0; i < s->mesh_count; ++i) {
    demo_upload_mesh(d, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; ++i) {
    demo_upload_texture(d, &s->textures[i]);
  }
}

void demo_process_event(Demo *d, const SDL_Event *e) {
  TracyCZoneN(ctx, "demo_process_event", true);
  TracyCZoneColor(ctx, TracyCategoryColorInput);
  ImGuiIO *io = d->ig_io;

  switch (e->type) {
  case SDL_MOUSEWHEEL: {
    if (e->wheel.x > 0)
      io->MouseWheelH += 1;
    if (e->wheel.x < 0)
      io->MouseWheelH -= 1;
    if (e->wheel.y > 0)
      io->MouseWheel += 1;
    if (e->wheel.y < 0)
      io->MouseWheel -= 1;
    break;
  }
  case SDL_MOUSEBUTTONDOWN: {
    // Don't handle global mouse events for now
    /*
    if (e->button.button == SDL_BUTTON_LEFT) {
      bd->MousePressed[0] = true;
    }
    if (e->button.button == SDL_BUTTON_RIGHT) {
      bd->MousePressed[1] = true;
    }
    if (e->button.button == SDL_BUTTON_MIDDLE) {
      bd->MousePressed[2] = true;
    }
    */
    break;
  }
  case SDL_TEXTINPUT: {
    ImGuiIO_AddInputCharactersUTF8(io, e->text.text);
    break;
  }
  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    uint64_t key = e->key.keysym.scancode;
    assert(key < sizeof(io->KeysDown));
    io->KeysDown[key] = (e->type == SDL_KEYDOWN);
    // io->KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
    // io->KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
    // io->KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
#ifdef _WIN32
    io->KeySuper = false;
#else
    io->KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
#endif
    break;
  }
  case SDL_WINDOWEVENT: {
    // TODO: Update ImGui to support this
    // if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
    //  ImGuiIO_AddFocusEvent(io, true);
    // else if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
    //  ImGuiIO_AddFocusEvent(io, false);
    break;
  }
  }

  TracyCZoneEnd(ctx);
}

void demo_resize(Demo *d) {
  TracyCZoneN(ctx, "demo_resize", true);
  VkResult err = vkDeviceWaitIdle(d->device);
  (void)err;

  d->swap_info = init_swapchain(d->window, d->device, d->gpu, d->surface,
                                &d->swapchain, d->vk_alloc, d->tmp_alloc);

  demo_init_image_views(d);

  demo_init_framebuffers(d);

  // Reset frame index so that the rendering routine knows that the
  // swapchain images need to be transitioned again
  d->frame_idx = 0;

  TracyCZoneEnd(ctx);
}

void demo_render_frame(Demo *d, const float4x4 *vp, const float4x4 *sky_vp) {
  TracyCZoneN(demo_render_frame_event, "demo_render_frame", true);

  VkResult err = VK_SUCCESS;

  VkDevice device = d->device;
  VkSwapchainKHR swapchain = d->swapchain;
  uint32_t frame_idx = d->frame_idx;

  VkFence *fences = d->fences;

  VkQueue graphics_queue = d->graphics_queue;
  VkQueue present_queue = d->present_queue;

  VkSemaphore img_acquired_sem = d->img_acquired_sems[frame_idx];
  VkSemaphore render_complete_sem = d->render_complete_sems[frame_idx];

  // Ensure no more than FRAME_LATENCY renderings are outstanding
  {
    TracyCZoneN(fence_ctx, "demo_render_frame wait for fence", true);
    TracyCZoneColor(fence_ctx, TracyCategoryColorWait);
    vkWaitForFences(device, 1, &fences[frame_idx], VK_TRUE, UINT64_MAX);
    TracyCZoneEnd(fence_ctx);

    vkResetFences(device, 1, &fences[frame_idx]);
  }

  // Acquire Image
  {
    TracyCZoneN(ctx, "demo_render_frame acquire next image", true);
    do {
      err =
          vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, img_acquired_sem,
                                VK_NULL_HANDLE, &d->swap_img_idx);
      if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        // demo->swapchain is out of date (e.g. the window was resized) and
        // must be recreated:
        demo_resize(d);
      } else if (err == VK_SUBOPTIMAL_KHR) {
        // demo->swapchain is not as optimal as it could be, but the
        // platform's presentation engine will still present the image
        // correctly.
        break;
      } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
        // If the surface was lost we could re-create it.
        // But the surface is owned by SDL2
        assert(err == VK_SUCCESS);
      } else {
        assert(err == VK_SUCCESS);
      }
    } while (err != VK_SUCCESS);
    TracyCZoneEnd(ctx);
  }

  uint32_t swap_img_idx = d->swap_img_idx;

  // Allocate per-object constant buffers
  {
    // Determine if we can fit the current number of needed obj const buffers
    // in the current allocated set of const buffer
    uint32_t obj_count = d->main_scene->entity_count;
    uint32_t max_obj_count =
        d->obj_const_buffer_block_count * CONST_BUFFER_BLOCK_SIZE;

    if (obj_count > max_obj_count) {
      // If we can't, determine how much more we need and how many blocks
      // that corresponds to
      uint32_t new_block_count = obj_count - max_obj_count;
      new_block_count /= CONST_BUFFER_BLOCK_SIZE;
      new_block_count += 1;

      // Realloc the block collection
      // This won't require reallocating the old blocks
      uint32_t old_block_count = d->obj_const_buffer_block_count;
      d->obj_const_buffer_block_count += new_block_count;
      d->obj_const_buffer_blocks =
          hb_realloc_nm_tp(d->std_alloc, d->obj_const_buffer_blocks,
                           d->obj_const_buffer_block_count, GPUConstBuffer *);

      for (uint32_t i = 0; i < new_block_count; ++i) {
        // Allocate and instantiate new blocks
        uint32_t block_idx = old_block_count + i;
        d->obj_const_buffer_blocks[block_idx] = hb_alloc_nm_tp(
            d->std_alloc, CONST_BUFFER_BLOCK_SIZE, GPUConstBuffer);

        for (uint32_t ii = 0; ii < CONST_BUFFER_BLOCK_SIZE; ++ii) {
          d->obj_const_buffer_blocks[block_idx][ii] = create_gpuconstbuffer(
              d->device, d->vma_alloc, d->vk_alloc, sizeof(CommonObjectData));
        }
      }
    }
  }

  // Allocate per-frame descriptor sets

  VkDescriptorSet *main_scene_object_sets = NULL;
  VkDescriptorSet *main_scene_material_sets = NULL;
  {
    TracyCZoneN(demo_manage_descriptor_sets,
                "demo_render_frame manage descriptor sets", true);
    uint32_t max_obj_count = d->main_scene->entity_count;
    uint32_t max_mat_count =
        d->main_scene->texture_count; // TODO : Do materials instead

    uint32_t total_set_count = max_obj_count + max_mat_count;

    // Determine if we need to resize the pool
    {
      uint32_t ub_count = max_obj_count + max_mat_count;
      uint32_t img_count = max_mat_count * 8; // Assume max 8 texs per material
      DynPoolState *pool_state = &d->dyn_desc_pool_states[frame_idx];
      if (pool_state->max_set_count == 0 ||
          ub_count >
              pool_state->pool_sizes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] ||
          img_count >
              pool_state->pool_sizes[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] ||
          total_set_count > pool_state->max_set_count) {
        TracyCZoneN(demo_resize_pool, "demo_render_frame resize pool", true);

        // Set the new state
        pool_state->pool_sizes[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] = ub_count;
        pool_state->pool_sizes[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] = img_count;
        pool_state->max_set_count = total_set_count;

        // Re-create the pool
        VkDescriptorPool dyn_pool = d->dyn_desc_pools[frame_idx];
        if (dyn_pool != VK_NULL_HANDLE) {
          vkDestroyDescriptorPool(d->device, dyn_pool, d->vk_alloc);
        }

        uint32_t pool_type_count = 0;
        pool_type_count += (ub_count > 0);
        pool_type_count += (img_count > 0);

        VkDescriptorPoolSize *pool_sizes =
            hb_alloc_nm_tp(d->tmp_alloc, pool_type_count, VkDescriptorPoolSize);

        uint32_t pool_type_idx = 0;
        if (ub_count > 0) {
          pool_sizes[pool_type_idx++] = (VkDescriptorPoolSize){
              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .descriptorCount = ub_count,
          };
        }
        if (img_count > 0) {
          pool_sizes[pool_type_idx++] = (VkDescriptorPoolSize){
              .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              .descriptorCount = img_count,
          };
        }

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = pool_state->max_set_count,
            .poolSizeCount = pool_type_count,
            .pPoolSizes = pool_sizes,
        };

        err = vkCreateDescriptorPool(d->device, &create_info, d->vk_alloc,
                                     &dyn_pool);
        assert(err == VK_SUCCESS);

        d->dyn_desc_pools[frame_idx] = dyn_pool;

        TracyCZoneEnd(demo_resize_pool);
      } else {
        TracyCZoneN(demo_reset_pool, "demo_render_frame reset pool", true);

        // If we didn't need to re-create the pool, we can just reset it
        err = vkResetDescriptorPool(d->device, d->dyn_desc_pools[frame_idx], 0);
        assert(err == VK_SUCCESS);

        TracyCZoneEnd(demo_reset_pool);
      }
      TracyCZoneEnd(demo_manage_descriptor_sets);
    }

    VkDescriptorSetLayout *set_layouts =
        hb_alloc_nm_tp(d->tmp_alloc, total_set_count, VkDescriptorSetLayout);

    VkDescriptorSetAllocateInfo set_allocs = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = d->dyn_desc_pools[frame_idx],
        .descriptorSetCount = total_set_count,
        .pSetLayouts = set_layouts,
    };
    // TEMP: mat count * 4 due to material descriptor settings
    uint32_t write_count = max_obj_count + (max_mat_count * 4);
    VkWriteDescriptorSet *set_writes =
        hb_alloc_nm_tp(d->tmp_alloc, write_count, VkWriteDescriptorSet);
    uint32_t set_idx = 0;
    uint32_t write_idx = 0;
    {
      main_scene_object_sets =
          hb_alloc_nm_tp(d->tmp_alloc, max_obj_count, VkDescriptorSet);

      for (uint32_t i = 0; i < max_obj_count; ++i) {
        set_layouts[set_idx++] = d->gltf_object_set_layout;

        // Find the per-object const buffer
        uint32_t block_idx = i / CONST_BUFFER_BLOCK_SIZE;
        uint32_t item_idx = i % CONST_BUFFER_BLOCK_SIZE;

        GPUConstBuffer *obj_const_buffer =
            &d->obj_const_buffer_blocks[block_idx][item_idx];

        VkDescriptorBufferInfo *object_info =
            hb_alloc_tp(d->tmp_alloc, VkDescriptorBufferInfo);
        *object_info = (VkDescriptorBufferInfo){obj_const_buffer->gpu.buffer, 0,
                                                obj_const_buffer->size};

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = object_info,
        };
        set_writes[write_idx++] = write;
      }
    }
    {
      main_scene_material_sets =
          hb_alloc_nm_tp(d->tmp_alloc, max_mat_count, VkDescriptorSet);

      VkDescriptorBufferInfo *buffer_info =
          hb_alloc_nm_tp(d->tmp_alloc, max_mat_count, VkDescriptorBufferInfo);
      for (uint32_t i = 0; i < max_mat_count; ++i) {
        buffer_info[i].buffer =
            d->main_scene->materials[i].const_buffer.gpu.buffer;
        buffer_info[i].offset = 0;
        buffer_info[i].range = d->main_scene->materials[i].const_buffer.size;
      }

      VkDescriptorImageInfo *tex_info =
          hb_alloc_nm_tp(d->tmp_alloc, max_mat_count, VkDescriptorImageInfo);
      for (uint32_t i = 0; i < max_mat_count; ++i) {
        uint32_t tex_ref = d->main_scene->materials[i].texture_refs[0];
        const GPUTexture *texture = &d->main_scene->textures[tex_ref];
        tex_info[i] = (VkDescriptorImageInfo){
            NULL, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      }

      for (uint32_t i = 0; i < max_mat_count; ++i) {
        set_layouts[set_idx++] = d->gltf_material_set_layout;

        set_writes[write_idx++] =
            (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info[i]},

        set_writes[write_idx++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &tex_info[i],
        };

        set_writes[write_idx++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &tex_info[i],
        };

        set_writes[write_idx++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &tex_info[i],
        };
      }
    }

    // Allocate Sets
    {
      TracyCZoneN(demo_allocate_sets, "demo_render_frame allocate sets", true);

      main_scene_object_sets =
          hb_alloc_nm_tp(d->tmp_alloc, total_set_count, VkDescriptorSet);
      main_scene_material_sets = &main_scene_object_sets[max_obj_count];

      err = vkAllocateDescriptorSets(d->device, &set_allocs,
                                     main_scene_object_sets);
      assert(err == VK_SUCCESS);

      TracyCZoneEnd(demo_allocate_sets);
    }

    // Assign sets to writes
    write_idx = 0;
    for (uint32_t i = 0; i < max_obj_count; ++i) {
      set_writes[write_idx++].dstSet = main_scene_object_sets[i];
    }
    for (uint32_t i = 0; i < max_mat_count; ++i) {
      set_writes[write_idx++].dstSet = main_scene_material_sets[i];
      set_writes[write_idx++].dstSet = main_scene_material_sets[i];
      set_writes[write_idx++].dstSet = main_scene_material_sets[i];
      set_writes[write_idx++].dstSet = main_scene_material_sets[i];
    }

    // Issue Writes
    vkUpdateDescriptorSets(d->device, write_count, set_writes, 0, NULL);
  }

  // Render
  {
    TracyCZoneN(demo_render_frame_render_event, "demo_render_frame render",
                true);

    VkCommandPool command_pool = d->command_pools[frame_idx];
    vkResetCommandPool(device, command_pool, 0);

    VkCommandBuffer upload_buffer = d->upload_buffers[frame_idx];
    VkCommandBuffer graphics_buffer = d->graphics_buffers[frame_idx];

    // Set names after resetting the parent pool
    {
      set_vk_name(device, (uint64_t)upload_buffer,
                  VK_OBJECT_TYPE_COMMAND_BUFFER, "upload command buffer");
      set_vk_name(device, (uint64_t)graphics_buffer,
                  VK_OBJECT_TYPE_COMMAND_BUFFER, "graphics command buffer");
    }

    TracyCGPUContext *gpu_gfx_ctx = d->tracy_gpu_contexts[frame_idx];

    VkSemaphore upload_sem = VK_NULL_HANDLE;

    // Record
    {
      TracyCZoneN(record_upload_event,
                  "demo_render_frame record upload commands", true);
      TracyCZoneColor(record_upload_event, TracyCategoryColorRendering);

      // Upload
      if (d->const_buffer_upload_count > 0 || d->mesh_upload_count > 0 ||
          d->texture_upload_count > 0) {
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        err = vkBeginCommandBuffer(upload_buffer, &begin_info);
        assert(err == VK_SUCCESS);

        TracyCVkNamedZone(gpu_gfx_ctx, upload_scope, upload_buffer, "Upload", 1,
                          true);
        cmd_begin_label(upload_buffer, "upload", (float4){0.1, 0.5, 0.1, 1.0});

        // Issue const buffer uploads
        if (d->const_buffer_upload_count > 0) {
          cmd_begin_label(upload_buffer, "upload const buffers",
                          (float4){0.1, 0.4, 0.1, 1.0});
          VkBufferCopy region = {0};
          for (uint32_t i = 0; i < d->const_buffer_upload_count; ++i) {
            GPUConstBuffer constbuffer = d->const_buffer_upload_queue[i];
            region = (VkBufferCopy){0, 0, constbuffer.size};
            vkCmdCopyBuffer(upload_buffer, constbuffer.host.buffer,
                            constbuffer.gpu.buffer, 1, &region);
          }
          d->const_buffer_upload_count = 0;
          cmd_end_label(upload_buffer);
        }

        // Issue mesh uploads
        if (d->mesh_upload_count > 0) {
          cmd_begin_label(upload_buffer, "upload meshes",
                          (float4){0.1, 0.4, 0.1, 1.0});
          VkBufferCopy region = {0};
          for (uint32_t i = 0; i < d->mesh_upload_count; ++i) {
            GPUMesh mesh = d->mesh_upload_queue[i];
            // Copy surfaces
            for (uint32_t ii = 0; ii < mesh.surface_count; ++ii) {
              const GPUSurface *surface = &mesh.surfaces[ii];
              region = (VkBufferCopy){0, 0, surface->size};
              vkCmdCopyBuffer(upload_buffer, surface->host.buffer,
                              surface->gpu.buffer, 1, &region);
            }
          }
          d->mesh_upload_count = 0;
          cmd_end_label(upload_buffer);
        }

        // Issue texture uploads
        if (d->texture_upload_count > 0) {
          cmd_begin_label(upload_buffer, "upload textures",
                          (float4){0.1, 0.4, 0.1, 1.0});
          VkImageMemoryBarrier barrier = {0};
          barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          barrier.subresourceRange.baseArrayLayer = 0;

          for (uint32_t i = 0; i < d->texture_upload_count; ++i) {
            GPUTexture tex = d->texture_upload_queue[i];

            VkImage image = tex.device.image;
            uint32_t img_width = tex.width;
            uint32_t img_height = tex.height;
            uint32_t mip_levels = tex.mip_levels;
            uint32_t layer_count = tex.layer_count;

            // Transition all mips to transfer dst
            {
              barrier.subresourceRange.baseMipLevel = 0;
              barrier.subresourceRange.levelCount = mip_levels;
              barrier.subresourceRange.layerCount = layer_count;
              barrier.srcAccessMask = 0;
              barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
              barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
              barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
              barrier.image = image;

              vkCmdPipelineBarrier(upload_buffer,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                                   0, NULL, 1, &barrier);

              // Afterwards, we're operating on single mips at a time no
              // matter what
              barrier.subresourceRange.levelCount = 1;
            }
            uint32_t region_count = tex.region_count;
            VkBufferImageCopy *regions = tex.regions;
            vkCmdCopyBufferToImage(upload_buffer, tex.host.buffer, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   region_count, regions);

            // Generate mipmaps
            if (tex.gen_mips) {
              uint32_t mip_width = img_width;
              uint32_t mip_height = img_height;

              for (uint32_t i = 1; i < mip_levels; ++i) {
                // Transition previous mip level to be transfer src
                {
                  barrier.subresourceRange.baseMipLevel = i - 1;
                  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                  vkCmdPipelineBarrier(upload_buffer,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                       NULL, 0, NULL, 1, &barrier);
                }

                // Copy to next mip
                VkImageBlit blit = {0};
                blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
                blit.srcOffsets[1] = (VkOffset3D){mip_width, mip_height, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = layer_count;
                blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
                blit.dstOffsets[1] =
                    (VkOffset3D){mip_width > 1 ? mip_width / 2 : 1,
                                 mip_height > 1 ? mip_height / 2 : 1, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = layer_count;

                vkCmdBlitImage(upload_buffer, image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                               VK_FILTER_LINEAR);

                // Transition input mip to shader read only
                {
                  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                  barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                  vkCmdPipelineBarrier(upload_buffer,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                       0, NULL, 0, NULL, 1, &barrier);
                }

                if (mip_width > 1) {
                  mip_width /= 2;
                }
                if (mip_height > 1) {
                  mip_height /= 2;
                }
              }
            }
            // Transition last subresource(s) to shader read
            {
              if (tex.gen_mips) {
                barrier.subresourceRange.baseMipLevel = mip_levels - 1;
              } else {
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = mip_levels;
              }
              barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
              barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
              barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
              barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
              barrier.image = image;
              vkCmdPipelineBarrier(upload_buffer,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                   NULL, 0, NULL, 1, &barrier);
            }
          }
          d->texture_upload_count = 0;
          cmd_end_label(upload_buffer);
        }

        // Issue Const Data Updates
        {
          // TODO: If sky data has changed only...
        }

        TracyCVkZoneEnd(upload_scope);
        TracyCVkCollect(gpu_gfx_ctx, upload_buffer);

        cmd_end_label(upload_buffer);

        err = vkEndCommandBuffer(upload_buffer);

        upload_sem = d->upload_complete_sems[frame_idx];
        assert(err == VK_SUCCESS);

        // Submit upload
        {
          VkSubmitInfo submit_info = {0};
          submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
          submit_info.commandBufferCount = 1;
          submit_info.pCommandBuffers = &upload_buffer;
          submit_info.signalSemaphoreCount = 1;
          submit_info.pSignalSemaphores = &upload_sem;

          queue_begin_label(d->graphics_queue, "upload",
                            (float4){0.1, 1.0, 0.1, 1.0});
          err = vkQueueSubmit(d->graphics_queue, 1, &submit_info, NULL);
          queue_end_label(d->graphics_queue);

          assert(err == VK_SUCCESS);
        }

        TracyCZoneEnd(record_upload_event);
      }

      VkCommandBufferBeginInfo begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      err = vkBeginCommandBuffer(graphics_buffer, &begin_info);
      assert(err == VK_SUCCESS);

      TracyCVkNamedZone(gpu_gfx_ctx, frame_scope, graphics_buffer, "Render", 1,
                        true);
      (void)gpu_gfx_ctx;

      // Transition Swapchain Image
      {
        VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (frame_idx >= FRAME_LATENCY) {
          old_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = old_layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = d->swapchain_images[frame_idx];
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(graphics_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1, &barrier);
      }

      // Render main geometry pass
      {
        VkFramebuffer framebuffer = d->main_pass_framebuffers[frame_idx];

        // Main Geometry Pass
        {
          TracyCVkNamedZone(gpu_gfx_ctx, main_scope, graphics_buffer,
                            "Main Pass", 2, true);
          const float width = d->swap_info.width;
          const float height = d->swap_info.height;

          cmd_begin_label(graphics_buffer, "main pass",
                          (float4){0.5, 0.1, 0.1, 1.0});

          // Set Render Pass
          {
            VkClearValue clear_values[2] = {
                {.color = {.float32 = {0, 1, 1, 1}}},
                {.depthStencil = {.depth = 0.0f, .stencil = 0.0f}},
            };

            VkRenderPassBeginInfo pass_info = {0};
            pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            pass_info.renderPass = d->render_pass;
            pass_info.framebuffer = framebuffer;
            pass_info.renderArea = (VkRect2D){{0, 0}, {width, height}};
            pass_info.clearValueCount = 2;
            pass_info.pClearValues = clear_values;

            vkCmdBeginRenderPass(graphics_buffer, &pass_info,
                                 VK_SUBPASS_CONTENTS_INLINE);
          }

          VkViewport viewport = {0, height, width, -height, 0, 1};
          VkRect2D scissor = {{0, 0}, {width, height}};
          vkCmdSetViewport(graphics_buffer, 0, 1, &viewport);
          vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

          // Draw Fullscreen Fractal
          // vkCmdBindPipeline(graphics_buffer,
          // VK_PIPELINE_BIND_POINT_GRAPHICS,
          //                    d->fractal_pipeline);
          // vkCmdDraw(graphics_buffer, 3, 1, 0, 0);

          // Draw Scene
          {
            // HACK: Known desired permutations
            uint32_t perm = GLTF_PERM_NONE;
            VkPipelineLayout pipe_layout = d->gltf_pipe_layout;
            VkPipeline pipe = d->gltf_pipeline->pipelines[perm];

            vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipe);

            TracyCVkNamedZone(gpu_gfx_ctx, scene_scope, graphics_buffer,
                              "Draw Scene", 3, true);

            demo_render_scene(d->main_scene, graphics_buffer, pipe_layout,
                              d->gltf_view_descriptor_sets[frame_idx],
                              main_scene_object_sets, main_scene_material_sets,
                              vp, d);

            TracyCVkZoneEnd(scene_scope);
          }

          // Draw Skydome
          {
            TracyCVkNamedZone(gpu_gfx_ctx, skydome_scope, graphics_buffer,
                              "Draw Skydome", 3, true);

            cmd_begin_label(graphics_buffer, "skydome",
                            (float4){0.4, 0.1, 0.1, 1.0});
            // Another hack to fiddle with the matrix we send to the shader
            // for the skydome
            SkyPushConstants sky_consts = {.vp = *sky_vp};
            vkCmdPushConstants(graphics_buffer, d->skydome_pipe_layout,
                               VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                               sizeof(SkyPushConstants),
                               (const void *)&sky_consts);

            uint32_t idx_count = d->skydome_gpu.surfaces[0].idx_count;

            vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              d->skydome_pipeline);

            vkCmdBindDescriptorSets(
                graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                d->skydome_pipe_layout, 0, 1,
                &d->skydome_descriptor_sets[frame_idx], 0, NULL);

            vkCmdBindDescriptorSets(graphics_buffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    d->skydome_pipe_layout, 1, 1,
                                    &d->hosek_descriptor_set, 0, NULL);

            VkBuffer b = d->skydome_gpu.surfaces[0].gpu.buffer;

            size_t idx_size = idx_count * sizeof(uint16_t) >>
                              d->skydome_gpu.surfaces[0].idx_type;

            VkBuffer buffers[1] = {b};
            VkDeviceSize offsets[1] = {idx_size};

            vkCmdBindIndexBuffer(graphics_buffer, b, 0, VK_INDEX_TYPE_UINT16);
            vkCmdBindVertexBuffers(graphics_buffer, 0, 1, buffers, offsets);
            vkCmdDrawIndexed(graphics_buffer, idx_count, 1, 0, 0, 0);

            cmd_end_label(graphics_buffer);
            TracyCVkZoneEnd(skydome_scope);
          }

          vkCmdEndRenderPass(graphics_buffer);

          cmd_end_label(graphics_buffer);

          TracyCVkZoneEnd(main_scope);
        }

        // ImGui Render Pass
        {
          TracyCVkNamedZone(gpu_gfx_ctx, imgui_scope, graphics_buffer, "ImGui",
                            2, true);
          // ImGui Internal Render
          {
            TracyCZoneN(ctx, "ImGui Internal", true);
            TracyCZoneColor(ctx, TracyCategoryColorUI);
            demo_imgui_update(d);
            igRender();
            TracyCZoneEnd(ctx);
          }

          const ImDrawData *draw_data = igGetDrawData();
          if (draw_data->Valid) {
            // (Re)Create and upload ImGui geometry buffer
            {
              TracyCZoneN(ctx, "ImGui Mesh Creation", true);
              TracyCZoneColor(ctx, TracyCategoryColorRendering);

              bool realloc = false;

              uint32_t idx_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
              uint32_t vtx_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
              // We know to use 8 for the alignment because the vertex
              // attribute layout starts with a float2
              const uint32_t alignment = 8;
              uint32_t align_padding = idx_size % alignment;

              uint32_t imgui_size = idx_size + align_padding + vtx_size;

              if (imgui_size > 0) {

                if (imgui_size > d->imgui_mesh_data_size[frame_idx]) {
                  destroy_gpumesh(d->vma_alloc, &d->imgui_gpu[frame_idx]);

                  d->imgui_mesh_data =
                      hb_realloc(d->std_alloc, d->imgui_mesh_data, imgui_size);
                  d->imgui_mesh_data_size[frame_idx] = imgui_size;

                  realloc = true;
                }

                uint8_t *idx_dst = d->imgui_mesh_data;
                uint8_t *vtx_dst = idx_dst + idx_size + align_padding;

                size_t test_size = 0;

                // Organize all mesh data into a single cpu-side buffer
                for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
                  const ImDrawList *cmd_list = draw_data->CmdLists[i];

                  size_t idx_byte_count =
                      cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
                  size_t vtx_byte_count =
                      cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);

                  test_size += idx_byte_count;
                  test_size += vtx_byte_count;

                  memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_byte_count);
                  memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_byte_count);

                  idx_dst += idx_byte_count;
                  vtx_dst += vtx_byte_count;
                }
                idx_dst = d->imgui_mesh_data;
                vtx_dst = idx_dst + idx_size + align_padding;

                assert(test_size + align_padding == imgui_size);
                (void)test_size;

                if (realloc) {
                  CPUMesh imgui_cpu = {.geom_size = vtx_size,
                                       .index_count = draw_data->TotalIdxCount,
                                       .index_size = idx_size,
                                       .indices = (uint16_t *)idx_dst,
                                       .vertex_count = draw_data->TotalVtxCount,
                                       .vertices = vtx_dst};

                  create_gpumesh(d->vma_alloc, &imgui_cpu,
                                 &d->imgui_gpu[frame_idx]);
                } else {
                  // Map existing gpu mesh and copy data
                  uint8_t *data = NULL;
                  vmaMapMemory(d->vma_alloc,
                               d->imgui_gpu[frame_idx].surfaces[0].host.alloc,
                               (void **)&data);

                  // Copy Data
                  memcpy(data, idx_dst, imgui_size);

                  vmaUnmapMemory(
                      d->vma_alloc,
                      d->imgui_gpu[frame_idx].surfaces[0].host.alloc);
                }

                // Copy to gpu
                {
                  VkBufferCopy region = {
                      .srcOffset = 0,
                      .dstOffset = 0,
                      .size = d->imgui_gpu[frame_idx].surfaces[0].size,
                  };
                  vkCmdCopyBuffer(
                      graphics_buffer,
                      d->imgui_gpu[frame_idx].surfaces[0].host.buffer,
                      d->imgui_gpu[frame_idx].surfaces[0].gpu.buffer, 1,
                      &region);
                }
              }

              TracyCZoneEnd(ctx);
            };

            // Record ImGui render commands
            {
              TracyCZoneN(ctx, "Record ImGui Commands", true);
              TracyCZoneColor(ctx, TracyCategoryColorRendering);

              cmd_begin_label(graphics_buffer, "imgui",
                              (float4){0.1, 0.1, 0.5, 1.0});

              const float width = d->ig_io->DisplaySize.x;
              const float height = d->ig_io->DisplaySize.y;

              // We know to use 8 for the alignment because the vertex
              // attribute layout starts with a float2
              const uint32_t alignment = 8;

              // Set Render Pass
              {
                VkFramebuffer framebuffer = d->ui_pass_framebuffers[frame_idx];

                VkClearValue clear_values[1] = {
                    {.color = {.float32 = {0, 0, 0, 0}}},
                };

                VkRenderPassBeginInfo pass_info = {0};
                pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                pass_info.renderPass = d->imgui_pass;
                pass_info.framebuffer = framebuffer;
                pass_info.renderArea = (VkRect2D){{0, 0}, {width, height}};
                pass_info.clearValueCount = 1;
                pass_info.pClearValues = clear_values;

                vkCmdBeginRenderPass(graphics_buffer, &pass_info,
                                     VK_SUBPASS_CONTENTS_INLINE);
              }

              // Draw ImGui
              {
                vkCmdBindPipeline(graphics_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  d->imgui_pipeline);

                // Bind the imgui atlas
                vkCmdBindDescriptorSets(
                    graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    d->imgui_pipe_layout, 0, 1,
                    &d->imgui_descriptor_sets[frame_idx], 0, NULL);

                VkViewport viewport = {0, 0, width, height, 0, 1};
                VkRect2D scissor = {{0, 0}, {width, height}};
                vkCmdSetViewport(graphics_buffer, 0, 1, &viewport);
                vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

                float scale_x = 2.0f / draw_data->DisplaySize.x;
                float scale_y = 2.0f / draw_data->DisplaySize.y;

                ImGuiPushConstants push_constants = {
                    .scale = {scale_x, scale_y},
                    .translation = {-1.0f - draw_data->DisplayPos.x * scale_x,
                                    -1.0f - draw_data->DisplayPos.y * scale_y},
                };
                vkCmdPushConstants(graphics_buffer, d->imgui_pipe_layout,
                                   VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                                   sizeof(ImGuiPushConstants),
                                   (const void *)&push_constants);

                GPUMesh *imgui_mesh = &d->imgui_gpu[frame_idx];

                uint32_t idx_offset = 0;
                uint32_t vtx_offset = 0;

                VkDeviceSize vtx_buffer_offset =
                    draw_data->TotalIdxCount * sizeof(ImDrawIdx);
                vtx_buffer_offset += vtx_buffer_offset % alignment;

                {
                  TracyCZoneN(draw_ctx, "Record ImGui Draw Commands", true);
                  TracyCZoneColor(draw_ctx, TracyCategoryColorRendering);

                  vkCmdBindIndexBuffer(
                      graphics_buffer, imgui_mesh->surfaces[0].gpu.buffer, 0,
                      (VkIndexType)imgui_mesh->surfaces[0].idx_type);
                  vkCmdBindVertexBuffers(graphics_buffer, 0, 1,
                                         &imgui_mesh->surfaces[0].gpu.buffer,
                                         &vtx_buffer_offset);

                  for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
                    const ImDrawList *draw_list = draw_data->CmdLists[i];

                    for (int32_t ii = 0; ii < draw_list->CmdBuffer.Size; ++ii) {
                      const ImDrawCmd *draw_cmd =
                          &draw_list->CmdBuffer.Data[ii];
                      // Set the scissor
                      ImVec4 clip_rect = draw_cmd->ClipRect;
                      scissor = (VkRect2D){
                          {(int32_t)clip_rect.x, (int32_t)clip_rect.y},
                          {(uint32_t)clip_rect.z, (uint32_t)clip_rect.w}};
                      vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

                      // Issue the draw
                      vkCmdDrawIndexed(graphics_buffer, draw_cmd->ElemCount, 1,
                                       draw_cmd->IdxOffset + idx_offset,
                                       draw_cmd->VtxOffset + vtx_offset, 0);
                    }

                    // Adjust offsets
                    idx_offset += draw_list->IdxBuffer.Size;
                    vtx_offset += draw_list->VtxBuffer.Size;
                  }

                  TracyCZoneEnd(draw_ctx);
                }
              }

              vkCmdEndRenderPass(graphics_buffer);

              cmd_end_label(graphics_buffer);

              TracyCZoneEnd(ctx);
            }
          }

          TracyCVkZoneEnd(imgui_scope);
        }
      }

      TracyCVkZoneEnd(frame_scope);

      TracyCVkCollect(gpu_gfx_ctx, graphics_buffer);

      err = vkEndCommandBuffer(graphics_buffer);
      assert(err == VK_SUCCESS);
    }

    // Submit
    {
      TracyCZoneN(demo_render_frame_submit_event, "demo_render_frame submit",
                  true);
      TracyCZoneColor(demo_render_frame_submit_event,
                      TracyCategoryColorRendering);

      uint32_t wait_sem_count = 0;
      VkSemaphore wait_sems[16] = {0};
      VkPipelineStageFlags wait_stage_flags[16] = {0};

      wait_sems[wait_sem_count] = img_acquired_sem;
      wait_stage_flags[wait_sem_count++] =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      if (upload_sem != VK_NULL_HANDLE) {
        wait_sems[wait_sem_count] = upload_sem;
        wait_stage_flags[wait_sem_count++] = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }

      {
        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = wait_sem_count;
        submit_info.pWaitSemaphores = wait_sems;
        submit_info.pWaitDstStageMask = wait_stage_flags;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &graphics_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_complete_sem;
        queue_begin_label(graphics_queue, "raster",
                          (float4){1.0, 0.1, 0.1, 1.0});
        err = vkQueueSubmit(graphics_queue, 1, &submit_info, fences[frame_idx]);
        queue_end_label(graphics_queue);
        assert(err == VK_SUCCESS);
      }

      TracyCZoneEnd(demo_render_frame_submit_event);
    }

    TracyCZoneEnd(demo_render_frame_render_event);
  }

  // Present
  {
    TracyCZoneN(demo_render_frame_present_event, "demo_render_frame present",
                true);
    TracyCZoneColor(demo_render_frame_present_event,
                    TracyCategoryColorRendering);

    VkSemaphore wait_sem = render_complete_sem;
    if (d->separate_present_queue) {
      VkSemaphore swapchain_sem = d->swapchain_image_sems[frame_idx];
      // If we are using separate queues, change image ownership to the
      // present queue before presenting, waiting for the draw complete
      // semaphore and signalling the ownership released semaphore when
      // finished
      VkSubmitInfo submit_info = {0};
      submit_info.waitSemaphoreCount = 1;
      submit_info.pWaitSemaphores = &d->render_complete_sems[frame_idx];
      submit_info.commandBufferCount = 1;
      // submit_info.pCommandBuffers =
      //    &d->swapchain_images[swap_img_idx].graphics_to_present_cmd;
      submit_info.signalSemaphoreCount = 1;
      submit_info.pSignalSemaphores = &swapchain_sem;
      err = vkQueueSubmit(present_queue, 1, &submit_info, VK_NULL_HANDLE);
      assert(err == VK_SUCCESS);

      wait_sem = swapchain_sem;
    }

    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &wait_sem;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &swap_img_idx;
    err = vkQueuePresentKHR(present_queue, &present_info);

    d->frame_idx = (frame_idx + 1) % FRAME_LATENCY;

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      // demo->swapchain is out of date (e.g. the window was resized) and
      // must be recreated:
      demo_resize(d);
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // demo->swapchain is not as optimal as it could be, but the platform's
      // presentation engine will still present the image correctly.
    } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
      // If the surface was lost we could re-create it.
      // But the surface is owned by SDL2
      assert(err == VK_SUCCESS);
    } else {
      assert(err == VK_SUCCESS);
    }

    TracyCZoneEnd(demo_render_frame_present_event);
  }

  TracyCZoneEnd(demo_render_frame_event);
}

bool demo_screenshot(Demo *d, Allocator std_alloc, uint8_t **screenshot_bytes,
                     uint32_t *screenshot_size) {
  TracyCZoneN(ctx, "demo_screenshot", true);
  VkResult err = VK_SUCCESS;

  VkDevice device = d->device;
  uint32_t frame_idx = d->frame_idx;
  VmaAllocator vma_alloc = d->vma_alloc;
  GPUImage screenshot_image = d->screenshot_image;
  VkImage swap_image = d->swapchain_images[frame_idx];
  VkFence swap_fence = d->fences[frame_idx];

  VkQueue queue = d->graphics_queue;

  VkFence screenshot_fence = d->screenshot_fence;
  VkCommandBuffer screenshot_cmd = d->screenshot_buffers[frame_idx];

  /*
    Only need to wait for this fence if we know it hasn't been signaled.
    As such we don't need to reset the fence, that will be done by another
    waiter.
  */
  VkResult status = vkGetFenceStatus(device, swap_fence);
  if (status == VK_NOT_READY) {
    TracyCZoneN(fence_ctx, "Wait for swap fence", true);
    TracyCZoneColor(fence_ctx, TracyCategoryColorWait);
    err = vkWaitForFences(device, 1, &swap_fence, VK_TRUE, ~0ULL);
    TracyCZoneEnd(fence_ctx);
    if (err != VK_SUCCESS) {
      TracyCZoneEnd(ctx);
      assert(0);
      return false;
    }
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  err = vkBeginCommandBuffer(screenshot_cmd, &begin_info);
  if (err != VK_SUCCESS) {
    TracyCZoneEnd(ctx);
    assert(0);
    return false;
  }

  // Issue necessary memory barriers
  VkImageMemoryBarrier barrier = {0};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange =
      (VkImageSubresourceRange){.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .levelCount = 1,
                                .layerCount = 1};
  {
    // Transition swap image from Present to Transfer Src
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = swap_image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);

    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlagBits src_access = 0;
    // If screenshot_bytes points to something not-null that means we've
    // made a screenshot before and can assume the old layout
    if ((*screenshot_bytes) != NULL) {
      old_layout = VK_IMAGE_LAYOUT_GENERAL;
      src_access = VK_ACCESS_MEMORY_READ_BIT;
    }

    // Transition screenshot image from General (or Undefined) to Transfer Dst
    barrier.oldLayout = old_layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = screenshot_image.image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
  }

  // Copy the swapchain image to a GPU to CPU image of a known format
  VkImageCopy image_copy = {
      .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .layerCount = 1},
      .srcOffset = {0, 0, 0},
      .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .layerCount = 1},
      .dstOffset = {0, 0, 0},
      .extent = {d->swap_info.width, d->swap_info.height, 1},
  };
  vkCmdCopyImage(screenshot_cmd, swap_image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screenshot_image.image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);

  // Issue necessary memory barriers back to original formats

  {
    // Transition swap image from to Transfer Src to Present
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = swap_image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);

    // Transition screenshot image from Transfer Dst to General
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.image = screenshot_image.image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
  }

  err = vkEndCommandBuffer(screenshot_cmd);
  if (err != VK_SUCCESS) {
    TracyCZoneEnd(ctx);
    assert(0);
    return false;
  }

  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &screenshot_cmd,
  };
  err = vkQueueSubmit(queue, 1, &submit_info, screenshot_fence);
  if (err != VK_SUCCESS) {
    TracyCZoneEnd(ctx);
    assert(0);
    return false;
  }

  // Could move this to another place later on as it will take time for this
  // command to finish
  err = vkWaitForFences(device, 1, &screenshot_fence, VK_TRUE, ~0ULL);
  if (err != VK_SUCCESS) {
    TracyCZoneEnd(ctx);
    assert(0);
    return false;
  }
  vkResetFences(device, 1, &screenshot_fence);

  VkImageSubresource sub_resource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
  };
  VkSubresourceLayout sub_resource_layout = {0};
  vkGetImageSubresourceLayout(device, screenshot_image.image, &sub_resource,
                              &sub_resource_layout);

  uint8_t *screenshot_mem = NULL;
  err =
      vmaMapMemory(vma_alloc, screenshot_image.alloc, (void **)&screenshot_mem);
  if (err != VK_SUCCESS) {
    TracyCZoneEnd(ctx);
    assert(0);
    return false;
  }

  VmaAllocationInfo alloc_info = {0};
  vmaGetAllocationInfo(vma_alloc, screenshot_image.alloc, &alloc_info);

  if (alloc_info.size > (*screenshot_size)) {
    (*screenshot_bytes) =
        hb_realloc(std_alloc, (*screenshot_bytes), alloc_info.size);
    (*screenshot_size) = alloc_info.size;
  }

  // Use SDL to transform raw bytes into a png bytestream
  {
    uint32_t rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0x0000ff00;
    gmask = 0x00ff0000;
    bmask = 0xff000000;
    amask = 0x000000ff;
#else // little endian, like x86
    rmask = 0x00ff0000;
    gmask = 0x0000ff00;
    bmask = 0x000000ff;
    amask = 0xff000000;
#endif
    // Note ^ We're assuming that the swapchain is BGR

    int32_t pitch = d->swap_info.width * 4;
    SDL_Surface *img = SDL_CreateRGBSurfaceFrom(
        (screenshot_mem + sub_resource_layout.offset), d->swap_info.width,
        d->swap_info.height, 32, pitch, rmask, gmask, bmask, amask);
    assert(img);

    SDL_RWops *ops =
        SDL_RWFromMem((void *)(*screenshot_bytes), sub_resource_layout.size);
    IMG_SavePNG_RW(img, ops, 0);

    SDL_FreeSurface(img);
  }

  vmaUnmapMemory(vma_alloc, screenshot_image.alloc);

  TracyCZoneEnd(ctx);
  return true;
}
