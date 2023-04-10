#include "rendertargetsystem.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

typedef struct RenderTargetMipView {
  VkExtent3D extent;
  VkImageView views[TB_MAX_FRAME_STATES];
} RenderTargetMipView;

typedef struct RenderTarget {
  bool imported;
  VkFormat format;
  TbImage images[TB_MAX_FRAME_STATES];
  VkImageView views[TB_MAX_FRAME_STATES];
  uint32_t mip_count;
  RenderTargetMipView mip_views[TB_MAX_MIPS];
} RenderTarget;

bool create_render_target(RenderTargetSystem *self, RenderTarget *rt,
                          const RenderTargetDescriptor *desc) {
  VkResult err = VK_SUCCESS;

  VkImageUsageFlagBits usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (desc->format == VK_FORMAT_D32_SFLOAT) {
    usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  // Must set a special flag if we want to make a cubemap
  VkImageCreateFlags create_flags = 0;
  if (desc->view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
      desc->view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
    create_flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }

  // Store the render target's format so we can look it up later
  TB_CHECK_RETURN(desc->format != VK_FORMAT_UNDEFINED,
                  "Undefined render target format", false);
  rt->format = desc->format;

  // Allocate images for each frame
  {
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      VkImageCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .flags = create_flags,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = desc->format,
          .extent = desc->extent,
          .mipLevels = desc->mip_count,
          .arrayLayers = desc->layer_count,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
      };
      err = tb_rnd_sys_alloc_gpu_image(self->render_system, &create_info,
                                       desc->name, &rt->images[i]);
      TB_VK_CHECK_RET(err, "Failed to allocate image for render target", false);

      rt->images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      char view_name[100] = {0};
      SDL_snprintf(view_name, 100, "%s View", desc->name);

      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .viewType = desc->view_type,
          .format = desc->format,
          .image = rt->images[i].image,
          .subresourceRange = {aspect, 0, desc->mip_count, 0,
                               desc->layer_count},
      };
      err = tb_rnd_create_image_view(self->render_system, &create_info,
                                     view_name, &rt->views[i]);
      TB_VK_CHECK_RET(err, "Failed to create image view for render target",
                      false);
    }

    // Create views for each mip and each frame
    rt->mip_count = desc->mip_count;
    for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
      RenderTargetMipView *mip_view = &rt->mip_views[mip_idx];

      mip_view->extent.width = desc->extent.width * SDL_powf(0.5f, mip_idx);
      mip_view->extent.height = desc->extent.height * SDL_powf(0.5f, mip_idx);
      mip_view->extent.depth = desc->extent.depth;

      for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
        char view_name[100] = {0};
        SDL_snprintf(view_name, 100, "%s Mip %d View", desc->name, mip_idx);

        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = desc->view_type,
            .format = desc->format,
            .image = rt->images[i].image,
            .subresourceRange = {aspect, mip_idx, 1, 0, desc->layer_count},
        };
        err = tb_rnd_create_image_view(self->render_system, &create_info,
                                       view_name, &mip_view->views[i]);
        TB_VK_CHECK_RET(err, "Failed to create image view for render target",
                        false);
      }
    }
  }
  return true;
}

void resize_render_target(RenderTargetSystem *self, RenderTarget *render_target,
                          RenderTargetDescriptor *desc) {
  // Clean up old images and views
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_free_gpu_image(self->render_system, &render_target->images[i]);
    tb_rnd_destroy_image_view(self->render_system, render_target->views[i]);
    for (uint32_t mip_idx = 0; mip_idx < render_target->mip_count; ++mip_idx) {
      tb_rnd_destroy_image_view(self->render_system,
                                render_target->mip_views[mip_idx].views[i]);
    }
  }

  // Re-create render target
  create_render_target(self, render_target, desc);
}

void reimport_render_target(RenderTargetSystem *self, TbRenderTargetId target,
                            const RenderTargetDescriptor *rt_desc,
                            const VkImage *images) {
  VkResult err = VK_SUCCESS;

  RenderTarget *rt = &self->render_targets[target];
  // HACK: Assuming only one mip here
  // Pretty much assuming a swapchain only

  // Clean up old views
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
      tb_rnd_destroy_image_view(self->render_system,
                                rt->mip_views[mip_idx].views[i]);
    }
  }

  rt->mip_count = 1;
  rt->mip_views[0].extent = rt_desc->extent;

  VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (rt_desc->format == VK_FORMAT_D32_SFLOAT) {
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = rt_desc->format,
        .image = images[i],
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    err = tb_rnd_create_image_view(self->render_system, &create_info,
                                   "Imported Render Target View",
                                   &rt->mip_views[0].views[i]);
    TB_VK_CHECK(err, "Failed to create image view for imported render target");
    rt->views[i] = rt->mip_views[0].views[i];
  }
}

bool create_render_target_system(RenderTargetSystem *self,
                                 const RenderTargetSystemDescriptor *desc,
                                 uint32_t system_dep_count,
                                 System *const *system_deps) {
  // Find necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which render targets depend on",
                  false);

  *self = (RenderTargetSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Create some default render targets
  {
    const Swapchain *swapchain = &render_system->render_thread->swapchain;
    const VkFormat swap_format = swapchain->format;
    const uint32_t width = swapchain->width;
    const uint32_t height = swapchain->height;

    // Create depth target
    {
      RenderTargetDescriptor rt_desc = {
          .name = "Depth Buffer",
          .format = VK_FORMAT_D32_SFLOAT,
          .extent =
              {
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      self->depth_buffer = tb_create_render_target(self, &rt_desc);
    }

    // Create hdr color target
    {
      RenderTargetDescriptor rt_desc = {
          .name = "HDR Color",
          .format = VK_FORMAT_R16G16B16A16_SFLOAT,
          .extent =
              {
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      self->hdr_color = tb_create_render_target(self, &rt_desc);
    }

    // Create depth copy target which has a different format
    {
      RenderTargetDescriptor rt_desc = {
          .name = "Depth Copy",
          .format = VK_FORMAT_R32_SFLOAT,
          .extent =
              {
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      self->depth_buffer_copy = tb_create_render_target(self, &rt_desc);
    }

    // Create color copy target
    {
      RenderTargetDescriptor rt_desc = {
          .name = "Color Copy",
          .format = swap_format,
          .extent =
              {
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      self->color_copy = tb_create_render_target(self, &rt_desc);
    }

    // Create sky capture cube
    {
      RenderTargetDescriptor rt_desc = {
          .name = "Sky Cubemap Capture",
          .format = VK_FORMAT_R16G16B16A16_SFLOAT,
          .extent =
              {
                  .width = 512,
                  .height = 512,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 6,
          .view_type = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      self->env_cube = tb_create_render_target(self, &rt_desc);
    }

    // Create irradiance map
    {
      RenderTargetDescriptor rt_desc = {
          .name = "Irradiance Map",
          .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .extent =
              {
                  .width = 64,
                  .height = 64,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 6,
          .view_type = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      self->irradiance_map = tb_create_render_target(self, &rt_desc);
    }

    // Create prefiltered env cubemap
    {
      const uint32_t mip_count = (uint32_t)(floorf(log2f(512.0f))) + 1u;
      RenderTargetDescriptor rt_desc = {
          .name = "Prefiltered Environment Map",
          .format = VK_FORMAT_R16G16B16A16_SFLOAT,
          .extent =
              {
                  .width = 512,
                  .height = 512,
                  .depth = 1,
              },
          .mip_count = mip_count,
          .layer_count = 6,
          .view_type = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      self->prefiltered_cube = tb_create_render_target(self, &rt_desc);
    }

    // Create shadow maps
    {
      RenderTargetDescriptor rt_desc = {
          .format = VK_FORMAT_D32_SFLOAT,
          .extent =
              {
                  .width = TB_SHADOW_MAP_DIM,
                  .height = TB_SHADOW_MAP_DIM,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
        char name[100] = {0};
        SDL_snprintf(name, 100, "Shadow Cascade %d", i);
        rt_desc.name = name;
        self->shadow_maps[i] = tb_create_render_target(self, &rt_desc);
      }
    }

    // Import swapchain target
    {
      RenderTargetDescriptor rt_desc = {
          .name = "Swapchain",
          .format = swap_format,
          .extent =
              {
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      VkImage images[TB_MAX_FRAME_STATES] = {0};
      for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
        images[i] =
            render_system->render_thread->frame_states[i].swapchain_image;
      }
      self->swapchain = tb_import_render_target(self, &rt_desc, images);
    }
  }

  return true;
}

void destroy_render_target_system(RenderTargetSystem *self) {
  // Destroy all render targets
  for (uint32_t rt_idx = 0; rt_idx < self->rt_count; ++rt_idx) {
    RenderTarget *rt = &self->render_targets[rt_idx];
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      if (!rt->imported) {
        // Imported targets are supposed to be cleaned up externally
        tb_rnd_free_gpu_image(self->render_system, &rt->images[i]);
      }
      tb_rnd_destroy_image_view(self->render_system, rt->views[i]);
    }
    if (!rt->imported) {
      for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
        for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
          RenderTargetMipView *mip_view = &rt->mip_views[mip_idx];
          tb_rnd_destroy_image_view(self->render_system, mip_view->views[i]);
        }
      }
    }
  }

  *self = (RenderTargetSystem){0};
}

void tick_render_target_system(RenderTargetSystem *self,
                               const SystemInput *input, SystemOutput *output,
                               float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(render_target, RenderTargetSystem,
                 RenderTargetSystemDescriptor)

void tb_render_target_system_descriptor(
    SystemDescriptor *desc, const RenderTargetSystemDescriptor *rt_desc) {
  *desc = (SystemDescriptor){
      .name = "Render Target",
      .size = sizeof(RenderTargetSystem),
      .id = RenderTargetSystemId,
      .desc = (InternalDescriptor)rt_desc,
      .dep_count = 0,
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_render_target_system,
      .destroy = tb_destroy_render_target_system,
      .tick = tb_tick_render_target_system,
  };
}

void tb_reimport_swapchain(RenderTargetSystem *self) {
  // Called when the swapchain resizes
  const Swapchain *swapchain = &self->render_system->render_thread->swapchain;
  const VkFormat swap_format = swapchain->format;
  const uint32_t width = swapchain->width;
  const uint32_t height = swapchain->height;
  {
    RenderTargetDescriptor rt_desc = {
        .name = "Depth Buffer",
        .format = VK_FORMAT_D32_SFLOAT,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mip_count = 1,
        .layer_count = 1,
        .view_type = VK_IMAGE_VIEW_TYPE_2D,
    };
    resize_render_target(self, &self->render_targets[self->depth_buffer],
                         &rt_desc);
  }
  {
    RenderTargetDescriptor rt_desc = {
        .name = "HDR Color",
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mip_count = 1,
        .layer_count = 1,
        .view_type = VK_IMAGE_VIEW_TYPE_2D,
    };
    resize_render_target(self, &self->render_targets[self->hdr_color],
                         &rt_desc);
  }
  {
    RenderTargetDescriptor rt_desc = {
        .name = "Depth Copy",
        .format = VK_FORMAT_R32_SFLOAT,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mip_count = 1,
        .layer_count = 1,
        .view_type = VK_IMAGE_VIEW_TYPE_2D,
    };
    resize_render_target(self, &self->render_targets[self->depth_buffer_copy],
                         &rt_desc);
  }
  {
    RenderTargetDescriptor rt_desc = {
        .name = "Color Copy",
        .format = swap_format,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mip_count = 1,
        .layer_count = 1,
        .view_type = VK_IMAGE_VIEW_TYPE_2D,
    };
    resize_render_target(self, &self->render_targets[self->color_copy],
                         &rt_desc);
  }

  // Finally reimport swapchain
  {
    RenderTargetDescriptor rt_desc = {
        .name = "Swapchain",
        .format = swap_format,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mip_count = 1,
        .layer_count = 1,
        .view_type = VK_IMAGE_VIEW_TYPE_2D,
    };
    VkImage images[TB_MAX_FRAME_STATES] = {0};
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      images[i] =
          self->render_system->render_thread->frame_states[i].swapchain_image;
    }
    reimport_render_target(self, self->swapchain, &rt_desc, images);
  }
}

TbRenderTargetId alloc_render_target(RenderTargetSystem *self) {
  TbRenderTargetId id = self->rt_count;
  // Must resize
  if (self->rt_count + 1 > self->rt_max) {
    const uint32_t new_max = (self->rt_count + 1) * 2;
    self->render_targets = tb_realloc_nm_tp(
        self->std_alloc, self->render_targets, new_max, RenderTarget);
    self->rt_max = new_max;
  }
  self->rt_count++;
  return id;
}

TbRenderTargetId tb_import_render_target(RenderTargetSystem *self,
                                         const RenderTargetDescriptor *rt_desc,
                                         const VkImage *images) {
  TbRenderTargetId id = alloc_render_target(self);

  VkResult err = VK_SUCCESS;

  RenderTarget *rt = &self->render_targets[id];

  VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (rt_desc->format == VK_FORMAT_D32_SFLOAT) {
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  TB_CHECK_RETURN(rt_desc->format != VK_FORMAT_UNDEFINED,
                  "Undefined render target format", false);
  rt->format = rt_desc->format;

  rt->imported = true;

  // When importing, the images are already created but we still want to record
  // them here in case some api caller wants to get the VkImage for memory
  // barrier or something
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    rt->images[i] = (TbImage){
        .image = images[i],
    };
  }

  // Then just create relevant views
  // Assume only one mip
  rt->mip_count = 1;
  rt->mip_views[0].extent = rt_desc->extent;
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = rt_desc->format,
        .image = images[i],
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    err = tb_rnd_create_image_view(self->render_system, &create_info,
                                   "Imported Render Target View",
                                   &rt->mip_views[0].views[i]);
    TB_VK_CHECK_RET(err,
                    "Failed to create image view for imported render target",
                    InvalidRenderTargetId);
    rt->views[i] = rt->mip_views[0].views[i];
  }

  return id;
}

TbRenderTargetId
tb_create_render_target(RenderTargetSystem *self,
                        const RenderTargetDescriptor *rt_desc) {

  TbRenderTargetId id = alloc_render_target(self);
  RenderTarget *rt = &self->render_targets[id];
  TB_CHECK_RETURN(create_render_target(self, rt, rt_desc),
                  "Failed to create render target", InvalidRenderTargetId);
  return id;
}

VkExtent3D tb_render_target_get_extent(RenderTargetSystem *self,
                                       TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", (VkExtent3D){0});
  }
  return self->render_targets[rt].mip_views[0].extent;
}

VkExtent3D tb_render_target_get_mip_extent(RenderTargetSystem *self,
                                           uint32_t mip, TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", (VkExtent3D){0});
  }
  return self->render_targets[rt].mip_views[mip].extent;
}

VkFormat tb_render_target_get_format(RenderTargetSystem *self,
                                     TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range",
                    VK_FORMAT_UNDEFINED);
  }
  return self->render_targets[rt].format;
}

VkImageView tb_render_target_get_view(RenderTargetSystem *self,
                                      uint32_t frame_idx, TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", VK_NULL_HANDLE);
  }
  return self->render_targets[rt].views[frame_idx];
}

VkImageView tb_render_target_get_mip_view(RenderTargetSystem *self,
                                          uint32_t mip, uint32_t frame_idx,
                                          TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", VK_NULL_HANDLE);
  }
  return self->render_targets[rt].mip_views[mip].views[frame_idx];
}

VkImage tb_render_target_get_image(RenderTargetSystem *self, uint32_t frame_idx,
                                   TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", VK_NULL_HANDLE);
  }
  return self->render_targets[rt].images[frame_idx].image;
}
