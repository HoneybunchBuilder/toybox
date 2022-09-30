#include "rendertargetsystem.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

typedef struct RenderTarget {
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
    const uint32_t width = self->render_system->render_thread->swapchain.width;
    const uint32_t height =
        self->render_system->render_thread->swapchain.height;

    {
      RenderTargetDescriptor rt_desc = {
          .format = VK_FORMAT_D32_SFLOAT,
          .extent =
              {
                  .width = width,
                  .height = height,
                  .depth = 1,
              },
      };
      self->depth_buffer = tb_create_render_target(self, &rt_desc);
    }
  }

  return true;
}

void destroy_render_target_system(RenderTargetSystem *self) {
  // Destroy all render targets
  for (uint32_t rt_idx = 0; rt_idx < self->rt_count; ++rt_idx) {
    RenderTarget *rt = &self->render_targets[rt_idx];
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      tb_rnd_free_gpu_image(self->render_system, &rt->images[i]);
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

TbRenderTargetId
tb_create_render_target(RenderTargetSystem *self,
                        const RenderTargetDescriptor *rt_desc) {

  TbRenderTargetId id = self->rt_count;
  // Must resize
  if (self->rt_count + 1 > self->rt_max) {
    const uint32_t new_max = (self->rt_count + 1) * 2;
    self->render_targets = tb_realloc_nm_tp(
        self->std_alloc, self->render_targets, new_max, RenderTarget);
    self->rt_max = new_max;
  }
  self->rt_count++;

  VkResult err = VK_SUCCESS;

  RenderTarget *rt = &self->render_targets[id];

  VkImageUsageFlagBits usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (rt_desc->format == VK_FORMAT_D32_SFLOAT) {
    usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  // Allocate images and create views for each frame
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    {
      VkImageCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = rt_desc->format,
          .extent = rt_desc->extent,
          .mipLevels = 1,
          .arrayLayers = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
      };
      err = tb_rnd_sys_alloc_gpu_image(self->render_system, &create_info,
                                       "Render Target", &rt->images[i]);
      TB_VK_CHECK_RET(err, "Failed to allocate image for render target",
                      InvalidRenderTargetId);

      rt->images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    {
      VkImageViewCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = rt_desc->format,
          .image = rt->images[i].image,
          .subresourceRange = {aspect, 0, 1, 0, 1},
      };
      err = tb_rnd_create_image_view(self->render_system, &create_info,
                                     "Render Target View", &rt->views[i]);
      TB_VK_CHECK_RET(err, "Failed to create image view for render target",
                      InvalidRenderTargetId);
    }
  }

  return id;
}
