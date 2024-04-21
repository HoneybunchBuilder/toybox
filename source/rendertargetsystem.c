#include "rendertargetsystem.h"

#include "bloom.h"
#include "common.hlsli"
#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

#include <math.h>

ECS_COMPONENT_DECLARE(TbRenderTargetSystem);

void tb_register_render_target_sys(TbWorld *world);
void tb_unregister_render_target_sys(TbWorld *world);

TB_REGISTER_SYS(tb, render_target, TB_RT_SYS_PRIO)

typedef struct RenderTargetMipView {
  VkExtent3D extent;
  VkImageView views[TB_MAX_FRAME_STATES];
} RenderTargetMipView;

typedef struct RenderTargetLayerViews {
  RenderTargetMipView mip_views[TB_MAX_MIPS];
} RenderTargetLayerViews;

typedef struct TbRenderTarget {
  bool imported;
  VkFormat format;
  TbImage images[TB_MAX_FRAME_STATES];
  VkImageView views[TB_MAX_FRAME_STATES];
  uint32_t layer_count;
  uint32_t mip_count;
  RenderTargetLayerViews layer_views[TB_MAX_LAYERS];
} TbRenderTarget;

bool create_render_target(TbRenderTargetSystem *self, TbRenderTarget *rt,
                          const TbRenderTargetDescriptor *desc) {
  VkResult err = VK_SUCCESS;

  VkImageUsageFlagBits usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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

  // Determine image type based on view type
  VkImageType image_type = VK_IMAGE_TYPE_2D;
  if (desc->view_type == VK_IMAGE_VIEW_TYPE_1D) {
    image_type = VK_IMAGE_TYPE_1D;
  }

  // Allocate images for each frame
  {
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      VkImageCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .flags = create_flags,
          .imageType = image_type,
          .format = desc->format,
          .extent = desc->extent,
          .mipLevels = desc->mip_count,
          .arrayLayers = desc->layer_count,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
      };
      VmaAllocationCreateFlags flags =
          VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
      err = tb_rnd_sys_alloc_gpu_image(self->rnd_sys, &create_info, flags,
                                       desc->name, &rt->images[i]);
      TB_VK_CHECK_RET(err, "Failed to allocate image for render target", false);

      rt->images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      char view_name[100] = {0};
      SDL_snprintf(view_name, 100, "%s TbView", desc->name); // NOLINT

      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .viewType = desc->view_type,
          .format = desc->format,
          .image = rt->images[i].image,
          .subresourceRange = {aspect, 0, desc->mip_count, 0,
                               desc->layer_count},
      };
      err = tb_rnd_create_image_view(self->rnd_sys, &create_info, view_name,
                                     &rt->views[i]);
      TB_VK_CHECK_RET(err, "Failed to create image view for render target",
                      false);
    }

    // Create views for each layer and each mip and each frame

    rt->mip_count = desc->mip_count;

    // Handle cubemaps which don't want a view per layer
    if (desc->view_type == VK_IMAGE_VIEW_TYPE_CUBE) {
      rt->layer_count = 1;

      for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
        RenderTargetMipView *mip_view = &rt->layer_views[0].mip_views[mip_idx];

        float mip_idxf = (float)mip_idx;

        mip_view->extent.width =
            (uint32_t)((float)desc->extent.width * SDL_powf(0.5f, mip_idxf));
        mip_view->extent.height =
            (uint32_t)((float)desc->extent.height * SDL_powf(0.5f, mip_idxf));
        mip_view->extent.depth = desc->extent.depth;

        for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
          char view_name[100] = {0};
          // NOLINTNEXTLINE
          SDL_snprintf(view_name, 100, "%s Mip %d TbView", desc->name, mip_idx);

          VkImageViewCreateInfo create_info = {
              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
              .viewType = desc->view_type,
              .format = desc->format,
              .image = rt->images[i].image,
              .subresourceRange = {aspect, mip_idx, 1, 0, desc->layer_count},
          };
          err = tb_rnd_create_image_view(self->rnd_sys, &create_info, view_name,
                                         &mip_view->views[i]);
          TB_VK_CHECK_RET(err, "Failed to create image view for render target",
                          false);
        }
      }

    } else {
      rt->layer_count = desc->layer_count;

      for (uint32_t layer = 0; layer < rt->layer_count; ++layer) {
        RenderTargetLayerViews *layer_view = &rt->layer_views[layer];
        for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
          RenderTargetMipView *mip_view = &layer_view->mip_views[mip_idx];

          float mip_idxf = (float)mip_idx;

          mip_view->extent.width =
              (uint32_t)((float)desc->extent.width * SDL_powf(0.5f, mip_idxf));
          mip_view->extent.height =
              (uint32_t)((float)desc->extent.height * SDL_powf(0.5f, mip_idxf));
          mip_view->extent.depth = desc->extent.depth;

          for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
            char view_name[100] = {0};
            // NOLINTNEXTLINE
            SDL_snprintf(view_name, 100, "%s Layer %d Mip %d TbView",
                         desc->name, layer, mip_idx);

            VkImageViewCreateInfo create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .viewType = desc->view_type,
                .format = desc->format,
                .image = rt->images[i].image,
                .subresourceRange = {aspect, mip_idx, 1, layer, 1},
            };
            err = tb_rnd_create_image_view(self->rnd_sys, &create_info,
                                           view_name, &mip_view->views[i]);
            TB_VK_CHECK_RET(
                err, "Failed to create image view for render target", false);
          }
        }
      }
    }
  }
  return true;
}

void resize_render_target(TbRenderTargetSystem *self,
                          TbRenderTarget *render_target,
                          TbRenderTargetDescriptor *desc) {
  // Clean up old images and views
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_free_gpu_image(self->rnd_sys, &render_target->images[i]);
    tb_rnd_destroy_image_view(self->rnd_sys, render_target->views[i]);
    for (uint32_t layer = 0; layer < render_target->layer_count; ++layer) {
      for (uint32_t mip_idx = 0; mip_idx < render_target->mip_count;
           ++mip_idx) {
        tb_rnd_destroy_image_view(
            self->rnd_sys,
            render_target->layer_views[layer].mip_views[mip_idx].views[i]);
      }
    }
  }

  // Re-create render target
  create_render_target(self, render_target, desc);
}

void reimport_render_target(TbRenderTargetSystem *self, TbRenderTargetId target,
                            const TbRenderTargetDescriptor *rt_desc,
                            const VkImage *images) {
  VkResult err = VK_SUCCESS;

  TbRenderTarget *rt = &TB_DYN_ARR_AT(self->render_targets, target);
  // HACK: Assuming only one mip here
  // Pretty much assuming a swapchain only

  // Clean up old views
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    for (uint32_t layer = 0; layer < rt->layer_count; ++layer) {
      for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
        tb_rnd_destroy_image_view(self->rnd_sys,
                                  rt->layer_views->mip_views[mip_idx].views[i]);
      }
    }
  }

  rt->mip_count = 1;
  rt->layer_count = 1;
  rt->layer_views[0].mip_views[0].extent = rt_desc->extent;

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
    err = tb_rnd_create_image_view(self->rnd_sys, &create_info,
                                   "Imported Render Target TbView",
                                   &rt->layer_views[0].mip_views[0].views[i]);
    TB_VK_CHECK(err, "Failed to create image view for imported render target");
    rt->views[i] = rt->layer_views[0].mip_views[0].views[i];
  }
}

TbRenderTargetSystem create_render_target_system(TbRenderSystem *rnd_sys,
                                                 TbAllocator gp_alloc,
                                                 TbAllocator tmp_alloc) {
  TbRenderTargetSystem sys = {
      .rnd_sys = rnd_sys,
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
  };

  TB_DYN_ARR_RESET(sys.render_targets, sys.gp_alloc, 8);

  // Create some default render targets
  {
    const TbSwapchain *swapchain = &rnd_sys->render_thread->swapchain;
    const VkFormat swap_format = swapchain->format;
    const uint32_t width = swapchain->width;
    const uint32_t height = swapchain->height;

    // Create depth target
    {
      TbRenderTargetDescriptor rt_desc = {
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
      sys.depth_buffer = tb_create_render_target(&sys, &rt_desc);
    }

    // Create normal prepass target
    {
      TbRenderTargetDescriptor rt_desc = {
          .name = "Normal Prepass Buffer",
          .format = VK_FORMAT_R8G8B8A8_UNORM,
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
      sys.normal_buffer = tb_create_render_target(&sys, &rt_desc);
    }
    // Create hdr color target
    {
      TbRenderTargetDescriptor rt_desc = {
          .name = "HDR Color",
          .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
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
      sys.hdr_color = tb_create_render_target(&sys, &rt_desc);
    }

    // Create depth copy target which has a different format
    {
      TbRenderTargetDescriptor rt_desc = {
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
      sys.depth_buffer_copy = tb_create_render_target(&sys, &rt_desc);
    }

    // Create color copy target
    {
      TbRenderTargetDescriptor rt_desc = {
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
      sys.color_copy = tb_create_render_target(&sys, &rt_desc);
    }

    // Create sky capture cube
    {
      const uint32_t mip_count = (uint32_t)(SDL_floorf(log2f(512.0f))) + 1u;
      TbRenderTargetDescriptor rt_desc = {
          .name = "Sky Cubemap Capture",
          .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
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
      sys.env_cube = tb_create_render_target(&sys, &rt_desc);
    }

    // Create irradiance map
    {
      TbRenderTargetDescriptor rt_desc = {
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
      sys.irradiance_map = tb_create_render_target(&sys, &rt_desc);
    }

    // Create prefiltered env cubemap
    {
      const uint32_t mip_count = (uint32_t)(SDL_floorf(log2f(512.0f))) + 1u;
      TbRenderTargetDescriptor rt_desc = {
          .name = "Prefiltered Environment Map",
          .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
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
      sys.prefiltered_cube = tb_create_render_target(&sys, &rt_desc);
    }

    // Create shadow map
    {
      TbRenderTargetDescriptor rt_desc = {
          .name = "Shadow Cascades",
          .format = VK_FORMAT_D32_SFLOAT,
          .extent =
              {
                  .width = TB_SHADOW_MAP_DIM,
                  .height = TB_SHADOW_MAP_DIM,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = TB_CASCADE_COUNT,
          .view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
      };
      sys.shadow_map = tb_create_render_target(&sys, &rt_desc);
    }
    // Create brightness target
    {
      TbRenderTargetDescriptor rt_desc = {
          .name = "Brightness",
          .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
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
      sys.brightness = tb_create_render_target(&sys, &rt_desc);
    }

    // Creating a bloom mip chain target for downscale / upscale blur
    {
      // Minimum size for bloom target ensures we always have 5 mips
      uint32_t bloom_width = SDL_max(width, 32);
      uint32_t bloom_height = SDL_max(height, 32);
      TbRenderTargetDescriptor rt_desc = {
          .name = "Bloom Mip Chain",
          .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
          .extent =
              {
                  .width = bloom_width,
                  .height = bloom_height,
                  .depth = 1,
              },
          .mip_count = TB_BLOOM_MIPS,
          .layer_count = 1,
          .view_type = VK_IMAGE_VIEW_TYPE_2D,
      };
      sys.bloom_mip_chain = tb_create_render_target(&sys, &rt_desc);
    }

    // Create target for tonemapping to output to that isn't a swapchain
    {
      TbRenderTargetDescriptor rt_desc = {
          .name = "LDR Target",
          .format = VK_FORMAT_R8G8B8A8_UNORM,
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
      sys.ldr_target = tb_create_render_target(&sys, &rt_desc);
    }
    // Import swapchain target
    {
      TbRenderTargetDescriptor rt_desc = {
          .name = "TbSwapchain",
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
        images[i] = rnd_sys->render_thread->frame_states[i].swapchain_image;
      }
      sys.swapchain = tb_import_render_target(&sys, &rt_desc, images);
    }
  }

  return sys;
}

void destroy_render_target_system(TbRenderTargetSystem *self) {
  // Destroy all render targets
  TB_DYN_ARR_FOREACH(self->render_targets, rt_idx) {
    TbRenderTarget *rt = &TB_DYN_ARR_AT(self->render_targets, rt_idx);
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      if (!rt->imported) {
        // Imported targets are supposed to be cleaned up externally
        tb_rnd_free_gpu_image(self->rnd_sys, &rt->images[i]);
      }
      tb_rnd_destroy_image_view(self->rnd_sys, rt->views[i]);
    }
    if (!rt->imported) {
      for (uint32_t layer = 0; layer < rt->layer_count; ++layer) {
        for (uint32_t mip_idx = 0; mip_idx < rt->mip_count; ++mip_idx) {
          for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
            RenderTargetMipView *mip_view =
                &rt->layer_views[layer].mip_views[mip_idx];
            tb_rnd_destroy_image_view(self->rnd_sys, mip_view->views[i]);
          }
        }
      }
    }
  }
  TB_DYN_ARR_DESTROY(self->render_targets);

  *self = (TbRenderTargetSystem){0};
}

void tb_register_render_target_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Render Target Sys", true);
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbRenderTargetSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto sys =
      create_render_target_system(rnd_sys, world->gp_alloc, world->tmp_alloc);
  ecs_singleton_set_ptr(ecs, TbRenderTargetSystem, &sys);

  TracyCZoneEnd(ctx);
}

void tb_unregister_render_target_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  tb_auto sys = ecs_singleton_get_mut(ecs, TbRenderTargetSystem);
  destroy_render_target_system(sys);
  ecs_singleton_remove(ecs, TbRenderTargetSystem);
}

void tb_reimport_swapchain(TbRenderTargetSystem *self) {
  // Called when the swapchain resizes
  const TbSwapchain *swapchain = &self->rnd_sys->render_thread->swapchain;
  const VkFormat swap_format = swapchain->format;
  const uint32_t width = swapchain->width;
  const uint32_t height = swapchain->height;
  {
    TbRenderTargetDescriptor rt_desc = {
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->depth_buffer),
        &rt_desc);
  }
  {
    TbRenderTargetDescriptor rt_desc = {
        .name = "Normal Prepass Buffer",
        .format = VK_FORMAT_R8G8B8A8_UNORM,
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->normal_buffer),
        &rt_desc);
  }
  {
    TbRenderTargetDescriptor rt_desc = {
        .name = "HDR Color",
        .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->hdr_color), &rt_desc);
  }
  {
    TbRenderTargetDescriptor rt_desc = {
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->depth_buffer_copy),
        &rt_desc);
  }
  {
    TbRenderTargetDescriptor rt_desc = {
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->color_copy), &rt_desc);
  }

  // Resize brightness target
  {
    TbRenderTargetDescriptor rt_desc = {
        .name = "Brightness",
        .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->brightness), &rt_desc);
  }
  // Resize bloom mip chain
  {
    // Minimum size for bloom target ensures we always have 4 mips
    uint32_t bloom_width = SDL_max(width, 64);
    uint32_t bloom_height = SDL_max(height, 64);
    TbRenderTargetDescriptor rt_desc = {
        .name = "Bloom Mip Chain",
        .format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        .extent =
            {
                .width = bloom_width,
                .height = bloom_height,
                .depth = 1,
            },
        .mip_count = TB_BLOOM_MIPS,
        .layer_count = 1,
        .view_type = VK_IMAGE_VIEW_TYPE_2D,
    };
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->bloom_mip_chain),
        &rt_desc);
  }
  // LDR target that tonemapping outputs to
  {
    TbRenderTargetDescriptor rt_desc = {
        .name = "LDR Target",
        .format = VK_FORMAT_R8G8B8A8_UNORM,
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
    resize_render_target(
        self, &TB_DYN_ARR_AT(self->render_targets, self->ldr_target), &rt_desc);
  }
  // Finally reimport swapchain
  {
    TbRenderTargetDescriptor rt_desc = {
        .name = "TbSwapchain",
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
      images[i] = self->rnd_sys->render_thread->frame_states[i].swapchain_image;
    }
    reimport_render_target(self, self->swapchain, &rt_desc, images);
  }
}

TbRenderTargetId alloc_render_target(TbRenderTargetSystem *self) {
  TbRenderTargetId id = TB_DYN_ARR_SIZE(self->render_targets);
  TbRenderTarget rt = {0};
  TB_DYN_ARR_APPEND(self->render_targets, rt);
  return id;
}

TbRenderTargetId
tb_import_render_target(TbRenderTargetSystem *self,
                        const TbRenderTargetDescriptor *rt_desc,
                        const VkImage *images) {
  TbRenderTargetId id = alloc_render_target(self);

  VkResult err = VK_SUCCESS;

  TbRenderTarget *rt = &TB_DYN_ARR_AT(self->render_targets, id);

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
  // Assume only one mip and one layer
  rt->mip_count = 1;
  rt->layer_count = 1;
  rt->layer_views[0].mip_views[0].extent = rt_desc->extent;
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = rt_desc->format,
        .image = images[i],
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    err = tb_rnd_create_image_view(self->rnd_sys, &create_info,
                                   "Imported Render Target TbView",
                                   &rt->layer_views[0].mip_views[0].views[i]);
    TB_VK_CHECK_RET(err,
                    "Failed to create image view for imported render target",
                    TbInvalidRenderTargetId);
    rt->views[i] = rt->layer_views[0].mip_views[0].views[i];
  }

  return id;
}

TbRenderTargetId
tb_create_render_target(TbRenderTargetSystem *self,
                        const TbRenderTargetDescriptor *rt_desc) {

  TbRenderTargetId id = alloc_render_target(self);
  TbRenderTarget *rt = &TB_DYN_ARR_AT(self->render_targets, id);
  bool ok = create_render_target(self, rt, rt_desc);
  TB_CHECK_RETURN(ok, "Failed to create render target",
                  TbInvalidRenderTargetId);
  return id;
}

uint32_t tb_render_target_get_mip_count(TbRenderTargetSystem *self,
                                        TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", 0xFFFFFFFF);
  return TB_DYN_ARR_AT(self->render_targets, rt).mip_count;
}

VkExtent3D tb_render_target_get_extent(TbRenderTargetSystem *self,
                                       TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", (VkExtent3D){0});
  return TB_DYN_ARR_AT(self->render_targets, rt)
      .layer_views[0]
      .mip_views[0]
      .extent;
}

VkExtent3D tb_render_target_get_mip_extent(TbRenderTargetSystem *self,
                                           uint32_t layer, uint32_t mip,
                                           TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", (VkExtent3D){0});
  return TB_DYN_ARR_AT(self->render_targets, rt)
      .layer_views[layer]
      .mip_views[mip]
      .extent;
}

VkFormat tb_render_target_get_format(TbRenderTargetSystem *self,
                                     TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", VK_FORMAT_UNDEFINED);
  return TB_DYN_ARR_AT(self->render_targets, rt).format;
}

VkImageView tb_render_target_get_view(TbRenderTargetSystem *self,
                                      uint32_t frame_idx, TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", VK_NULL_HANDLE);

  return TB_DYN_ARR_AT(self->render_targets, rt).views[frame_idx];
}

VkImageView tb_render_target_get_mip_view(TbRenderTargetSystem *self,
                                          uint32_t layer, uint32_t mip,
                                          uint32_t frame_idx,
                                          TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", VK_NULL_HANDLE);
  return TB_DYN_ARR_AT(self->render_targets, rt)
      .layer_views[layer]
      .mip_views[mip]
      .views[frame_idx];
}

VkImage tb_render_target_get_image(TbRenderTargetSystem *self,
                                   uint32_t frame_idx, TbRenderTargetId rt) {
  TB_CHECK_RETURN(rt < TB_DYN_ARR_SIZE(self->render_targets),
                  "Render target index out of range", VK_NULL_HANDLE);
  return TB_DYN_ARR_AT(self->render_targets, rt).images[frame_idx].image;
}
