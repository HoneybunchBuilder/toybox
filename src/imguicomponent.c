#include "imguicomponent.h"

#include "tbcommon.h"
#include "tbimgui.h"
#include "tbvma.h"
#include "vkdbg.h"
#include "world.h"

bool create_imgui_component(ImGuiComponent *self,
                            const ImGuiComponentDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  // Ensure we have a reference to the render system
  RenderSystem *render_system = NULL;
  for (uint32_t i = 0; i < system_dep_count; ++i) {
    System *sys = system_deps[i];
    if (sys->id == RenderSystemId) {
      render_system = (RenderSystem *)((System *)sys)->self;
      break;
    }
  }

  TB_CHECK_RETURN(render_system, "Failed to get render system reference",
                  false);

  *self = (ImGuiComponent){
      .context = igCreateContext(desc->font_atlas),
  };

  VkResult err = VK_SUCCESS;

  // Get atlas texture data for this context
  ImGuiIO *io = igGetIO();

  uint8_t *pixels = NULL;
  int32_t tex_w = 0;
  int32_t tex_h = 0;
  int32_t bytes_pp = 0;
  ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &tex_w, &tex_h, &bytes_pp);

  // Create the atlas image on the GPU
  {
    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .arrayLayers = 1,
        .extent =
            (VkExtent3D){
                .width = tex_w,
                .height = tex_h,
                .depth = 1,
            },
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .imageType = VK_IMAGE_TYPE_2D,
        .mipLevels = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };

    err = tb_rnd_sys_alloc_gpu_image(render_system, &create_info, "ImGui Atlas",
                                     &self->atlas);
    TB_VK_CHECK_RET(err, "Failed to alloc imgui atlas", false);
  }

  // Get space for the image on the tmp buffer
  TbBuffer host_buf;
  {
    const uint64_t atlas_size = self->atlas.info.size;
    err =
        tb_rnd_sys_alloc_tmp_host_buffer(render_system, atlas_size, &host_buf);
    TB_VK_CHECK_RET(err, "Failed to alloc imgui atlas in tmp host buffer",
                    false);

    SDL_memcpy(host_buf.ptr, pixels, atlas_size);
  }

  // Copy the image from the tmp gpu buffer to the gpu image
  {
    // A bit jank, but upload the image directly from the gpu buffer that we
    // know will be copied to from the tmp host buffer before this copy
    // is completed.
    BufferImageCopy upload = {
        .src =
            render_system->render_thread->frame_states[render_system->frame_idx]
                .tmp_gpu_buffer,
        .dst = self->atlas.image,
        .region =
            {
                .bufferOffset = host_buf.offset,
                .imageSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .layerCount = 1,
                    },
                .imageExtent =
                    {
                        .width = tex_w,
                        .height = tex_h,
                        .depth = 1,
                    },
            },

    };
    tb_rnd_upload_buffer_to_image(render_system, &upload, 1);
  }

  // Create Image View for atlas
  {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = self->atlas.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .components =
            {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange =
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
    };
    err =
        vkCreateImageView(render_system->render_thread->device, &create_info,
                          &render_system->vk_host_alloc_cb, &self->atlas_view);
    TB_VK_CHECK_RET(err, "Failed to create imgui atlas view", false);
    SET_VK_NAME(render_system->render_thread->device, self->atlas_view,
                VK_OBJECT_TYPE_IMAGE_VIEW, "ImGui Atlas");
  }

  // Setup basic display size
  io->DisplaySize = (ImVec2){800.0f, 600.0f};
  io->DeltaTime = 0.1666667f;

  // For clean-up
  self->render_system = render_system;

  igNewFrame();
  return true;
}

void destroy_imgui_component(ImGuiComponent *self) {
  tb_rnd_free_gpu_image(self->render_system, &self->atlas);
  vkDestroyImageView(self->render_system->render_thread->device,
                     self->atlas_view, &self->render_system->vk_host_alloc_cb);

  igDestroyContext(self->context);
  *self = (ImGuiComponent){0};
}

TB_DEFINE_COMPONENT(imgui, ImGuiComponent, void)

void tb_imgui_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "ImGui",
      .size = sizeof(ImGuiComponent),
      .id = ImGuiComponentId,
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_imgui_component,
      .destroy = tb_destroy_imgui_component,
  };
}
