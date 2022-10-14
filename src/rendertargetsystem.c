#include "rendertargetsystem.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

typedef struct RenderTarget {
  bool imported;
  VkExtent3D extent;
  TbImage images[TB_MAX_FRAME_STATES];
  VkImageView views[TB_MAX_FRAME_STATES];
} RenderTarget;

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
                  .width = 256,
                  .height = 256,
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
          .format = VK_FORMAT_R16G16B16A16_SFLOAT,
          .extent =
              {
                  .width = 128,
                  .height = 128,
                  .depth = 1,
              },
          .mip_count = 1,
          .layer_count = 6,
          .view_type = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      self->irradiance_map = tb_create_render_target(self, &rt_desc);
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

  rt->extent = rt_desc->extent;
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
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = rt_desc->format,
        .image = images[i],
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    err =
        tb_rnd_create_image_view(self->render_system, &create_info,
                                 "Imported Render Target View", &rt->views[i]);
    TB_VK_CHECK_RET(err,
                    "Failed to create image view for imported render target",
                    InvalidRenderTargetId);
  }

  return id;
}

TbRenderTargetId
tb_create_render_target(RenderTargetSystem *self,
                        const RenderTargetDescriptor *rt_desc) {

  TbRenderTargetId id = alloc_render_target(self);

  VkResult err = VK_SUCCESS;

  RenderTarget *rt = &self->render_targets[id];

  VkImageUsageFlagBits usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (rt_desc->format == VK_FORMAT_D32_SFLOAT) {
    usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  rt->extent = rt_desc->extent;

  // Must set a special flag if we want to make a cubemap
  VkImageCreateFlags create_flags = 0;
  if (rt_desc->view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
      rt_desc->view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
    create_flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }

  // Allocate images and create views for each frame
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    {
      VkImageCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .flags = create_flags,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = rt_desc->format,
          .extent = rt_desc->extent,
          .mipLevels = 1,
          .arrayLayers = rt_desc->layer_count,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
      };
      err = tb_rnd_sys_alloc_gpu_image(self->render_system, &create_info,
                                       rt_desc->name, &rt->images[i]);
      TB_VK_CHECK_RET(err, "Failed to allocate image for render target",
                      InvalidRenderTargetId);

      rt->images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    {
      static const uint32_t name_max = 100;
      char view_name[name_max] = {0};
      SDL_snprintf(view_name, name_max, "%s View", rt_desc->name);

      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .viewType = rt_desc->view_type,
          .format = rt_desc->format,
          .image = rt->images[i].image,
          .subresourceRange = {aspect, 0, rt_desc->mip_count, 0,
                               rt_desc->layer_count},
      };
      err = tb_rnd_create_image_view(self->render_system, &create_info,
                                     view_name, &rt->views[i]);
      TB_VK_CHECK_RET(err, "Failed to create image view for render target",
                      InvalidRenderTargetId);
    }
  }

  return id;
}

VkExtent3D tb_render_target_get_extent(RenderTargetSystem *self,
                                       TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", (VkExtent3D){0});
  }
  return self->render_targets[rt].extent;
}

VkImageView tb_render_target_get_view(RenderTargetSystem *self,
                                      uint32_t frame_idx, TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", VK_NULL_HANDLE);
  }
  return self->render_targets[rt].views[frame_idx];
}

VkImage tb_render_target_get_image(RenderTargetSystem *self, uint32_t frame_idx,
                                   TbRenderTargetId rt) {
  if (rt >= self->rt_count) {
    TB_CHECK_RETURN(false, "Render target index out of range", VK_NULL_HANDLE);
  }
  return self->render_targets[rt].images[frame_idx].image;
}
