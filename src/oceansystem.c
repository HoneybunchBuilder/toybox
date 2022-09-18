#include "oceansystem.h"

#include "assets.h"
#include "cgltf.h"
#include "meshsystem.h"
#include "oceancomponent.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

void ocean_pass_record(VkCommandBuffer buffer, uint32_t batch_count,
                       const void *batches) {
  (void)buffer;
  (void)batch_count;
  (void)batches;
}

VkResult create_ocean_pipeline(RenderSystem *render_system, VkRenderPass pass,
                               VkPipelineLayout pipe_layout,
                               VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule ocean_vert = VK_NULL_HANDLE;
  VkShaderModule ocean_frag = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    err = tb_rnd_create_shader(render_system, &create_info, "Ocean Vert",
                               &ocean_vert);
    TB_VK_CHECK_RET(err, "Failed to load ocean vert shader module", err);

    err = tb_rnd_create_shader(render_system, &create_info, "Ocean Frag",
                               &ocean_frag);
    TB_VK_CHECK_RET(err, "Failed to load ocean frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .layout = pipe_layout,
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Ocean Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean pipeline", err);

  tb_rnd_destroy_shader(render_system, ocean_vert);
  tb_rnd_destroy_shader(render_system, ocean_frag);

  return err;
}

bool create_ocean_system(OceanSystem *self, const OceanSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which ocean depends on", false);
  MeshSystem *mesh_system =
      tb_get_system(system_deps, system_dep_count, MeshSystem);
  TB_CHECK_RETURN(mesh_system,
                  "Failed to find mesh system which ocean depends on", false);

  *self = (OceanSystem){
      .render_system = render_system,
      .mesh_system = mesh_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Load the known glb that has the ocean mesh
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(self->tmp_alloc, "scenes/Ocean.glb");

  // Load glb off disk
  cgltf_data *data = tb_read_glb(self->std_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  // Parse expected mesh from glb
  self->ocean_patch_mesh =
      tb_mesh_system_load_mesh(mesh_system, asset_path, &data->meshes[0]);

  VkResult err = VK_SUCCESS;

  // Create ocean pipeline layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Ocean Pipeline Layout",
                                        &self->pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean pipeline layout", err);
  }

  // Create render pass for the ocean
  {
    const uint32_t attachment_count = 2;
    VkAttachmentDescription attachments[attachment_count] = {
        {
            .format = render_system->render_thread->swapchain.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    const uint32_t color_ref_count = 1;
    VkAttachmentReference color_refs[color_ref_count] = {
        {
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };
    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses =
            &(VkSubpassDescription){
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = color_ref_count,
                .pColorAttachments = color_refs,
                .pDepthStencilAttachment =
                    &(VkAttachmentReference){
                        1,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    },
            },
        .pDependencies =
            &(VkSubpassDependency){
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            },
    };
    err = tb_rnd_create_render_pass(render_system, &create_info, "Ocean Pass",
                                    &self->ocean_pass);
    TB_VK_CHECK_RET(err, "Failed to create ocean pass", err);
  }

  // Create framebuffers for ocean pass
  {
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      const uint32_t attachment_count = 2;
      // TODO: Figure out a way to do this without referencing the render thread
      // directly
      VkImageView attachments[attachment_count] = {
          render_system->render_thread->frame_states[i].swapchain_image_view,
          render_system->render_thread->frame_states[i].depth_buffer_view,
      };

      VkFramebufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .renderPass = self->ocean_pass,
          .attachmentCount = attachment_count,
          .pAttachments = attachments,
          .width = render_system->render_thread->swapchain.width,
          .height = render_system->render_thread->swapchain.height,
          .layers = 1,
      };
      err = tb_rnd_create_framebuffer(render_system, &create_info,
                                      "Ocean Pass Framebuffer",
                                      &self->framebuffers[i]);
    }
  }

  // Register the render pass
  tb_rnd_register_pass(render_system, self->ocean_pass, self->framebuffers,
                       render_system->render_thread->swapchain.width,
                       render_system->render_thread->swapchain.height,
                       ocean_pass_record);

  return true;
}

void destroy_ocean_system(OceanSystem *self) {
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->ocean_patch_mesh);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_framebuffer(self->render_system, self->framebuffers[i]);
  }

  tb_rnd_destroy_render_pass(self->render_system, self->ocean_pass);
  tb_rnd_destroy_pipe_layout(self->render_system, self->pipe_layout);

  *self = (OceanSystem){0};
}

void tick_ocean_system(OceanSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(ocean, OceanSystem, OceanSystemDescriptor)

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc) {
  *desc = (SystemDescriptor){
      .name = "Ocean",
      .size = sizeof(OceanSystem),
      .id = OceanSystemId,
      .desc = (InternalDescriptor)ocean_desc,
      .dep_count = 1,
      .deps[0] = (SystemComponentDependencies){1, {OceanComponentId}},
      .system_dep_count = 2,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = MeshSystemId,
      .create = tb_create_ocean_system,
      .destroy = tb_destroy_ocean_system,
      .tick = tb_tick_ocean_system,
  };
}
