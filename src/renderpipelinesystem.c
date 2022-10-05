#include "renderpipelinesystem.h"

#include "profiling.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "world.h"

static const uint32_t MaxRenderPassDependencies = 4;
static const uint32_t MaxRenderPassAttachments = 4;

typedef struct RenderPass {
  uint32_t dep_count;
  TbRenderTargetId deps[MaxRenderPassDependencies];
  VkRenderPass pass;
  uint32_t attach_count;
  TbRenderTargetId attachments[MaxRenderPassAttachments];
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];
} RenderPass;

void register_pass(RenderPipelineSystem *self, RenderThread *thread,
                   TbRenderPassId id) {
  RenderPass *pass = &self->render_passes[id];
  Allocator std_alloc = self->std_alloc;
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state = &thread->frame_states[frame_idx];

    const uint32_t new_count = state->pass_ctx_count + 1;
    if (new_count > state->pass_ctx_max) {
      const uint32_t new_max = new_count * 2;
      state->pass_contexts = tb_realloc_nm_tp(std_alloc, state->pass_contexts,
                                              new_max, PassContext);
      state->pass_ctx_max = new_max;
    }

    VkFramebuffer framebuffer = pass->framebuffers[frame_idx];
    TbRenderTargetId target_id = pass->attachments[0];
    VkExtent3D target_ext =
        tb_render_target_get_extent(self->render_target_system, target_id);

    state->pass_contexts[state->pass_ctx_count] = (PassContext){
        .id = id,
        .pass = pass->pass,
        .attachment_count = pass->attach_count,
        .framebuffer = framebuffer,
        .width = target_ext.width,
        .height = target_ext.height,
    };
    state->pass_ctx_count = new_count;
  }
}

TbRenderPassId create_render_pass(
    RenderPipelineSystem *self, const VkRenderPassCreateInfo *create_info,
    uint32_t dep_count, const TbRenderTargetId *deps, uint32_t attach_count,
    const TbRenderTargetId *attachments, const char *name) {
  TbRenderPassId id = self->pass_count;
  uint32_t new_count = self->pass_count + 1;
  if (new_count > self->pass_max) {
    // Reallocate collection
    const uint32_t new_max = new_count * 2;
    self->render_passes = tb_realloc_nm_tp(self->std_alloc, self->render_passes,
                                           new_max, RenderPass);
    self->pass_max = new_max;
  }
  self->pass_count = new_count;

  RenderPass *pass = &self->render_passes[id];

  VkResult err = VK_SUCCESS;
  err = tb_rnd_create_render_pass(self->render_system, create_info, name,
                                  &pass->pass);
  TB_VK_CHECK_RET(err, "Failed to create render pass", InvalidRenderPassId);

  // Copy dependencies
  pass->dep_count = dep_count;
  SDL_memset(pass->deps, InvalidRenderPassId,
             sizeof(TbRenderPassId) * MaxRenderPassDependencies);
  if (dep_count > 0) {
    SDL_memcpy(pass->deps, deps, sizeof(TbRenderPassId) * dep_count);
  }

  // Copy attachments
  pass->attach_count = attach_count;
  SDL_memset(pass->attachments, InvalidRenderTargetId,
             sizeof(TbRenderTargetId) * MaxRenderPassAttachments);
  if (attach_count > 0) {
    SDL_memcpy(pass->attachments, attachments,
               sizeof(TbRenderTargetId) * attach_count);
  }

  // Create framebuffers for render target based on attachments
  {
    RenderTargetSystem *rt_sys = self->render_target_system;
    // HACK: Assume all attachments have the same extents
    const VkExtent3D extent =
        tb_render_target_get_extent(rt_sys, attachments[0]);
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      VkImageView attach_views[MaxRenderPassAttachments] = {0};

      for (uint32_t attach_idx = 0; attach_idx < attach_count; ++attach_idx) {
        attach_views[attach_idx] =
            tb_render_target_get_view(rt_sys, i, attachments[attach_idx]);
      }

      VkFramebufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .renderPass = pass->pass,
          .attachmentCount = attach_count,
          .pAttachments = attach_views,
          .width = extent.width,
          .height = extent.height,
          .layers = extent.depth,
      };
      err =
          tb_rnd_create_framebuffer(self->render_system, &create_info,
                                    "Pass Framebuffer", &pass->framebuffers[i]);
      TB_VK_CHECK_RET(err, "Failed to create pass framebuffer",
                      InvalidRenderPassId);
    }
  }

  return id;
}

bool create_render_pipeline_system(RenderPipelineSystem *self,
                                   const RenderPipelineSystemDescriptor *desc,
                                   uint32_t system_dep_count,
                                   System *const *system_deps) {
  // Find necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(
      render_system,
      "Failed to find render system which the render pipeline depends on",
      false);
  RenderTargetSystem *render_target_system =
      tb_get_system(system_deps, system_dep_count, RenderTargetSystem);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find render target system which the render "
                  "pipeline depends on",
                  false);

  *self = (RenderPipelineSystem){
      .render_system = render_system,
      .render_target_system = render_target_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Create some default passes
  {
    // Look up the render targets we know will be needed
    const TbRenderTargetId opaque_depth = render_target_system->depth_buffer;
    const TbRenderTargetId depth_copy = render_target_system->depth_buffer_copy;
    const TbRenderTargetId color_target = render_target_system->swapchain;
    const TbRenderTargetId transparent_depth =
        render_target_system->transparent_depth_buffer;

    // Create opaque depth pass
    {
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_D32_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .finalLayout =
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              },
          .subpassCount = 1,
          .pSubpasses =
              &(VkSubpassDescription){
                  .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                  .pDepthStencilAttachment =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              },
      };

      TbRenderPassId id = create_render_pass(
          self, &create_info, 0, NULL, 1, &opaque_depth, "Opaque Depth Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create opaque depth pass", false);
      self->opaque_depth_pass = id;
    }
    // Create opaque color pass
    {
      const uint32_t attachment_count = 2;
      VkAttachmentDescription attachments[attachment_count] = {
          {
              .format = self->render_system->render_thread->swapchain.format,
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
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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
                  .colorAttachmentCount = 1,
                  .pColorAttachments =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      },
                  .pDepthStencilAttachment =
                      &(VkAttachmentReference){
                          1,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              },
      };
      TbRenderPassId id =
          create_render_pass(self, &create_info, 1, &self->opaque_depth_pass, 2,
                             (TbRenderTargetId[2]){color_target, opaque_depth},
                             "Opaque Color Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create opaque color pass", false);
      self->opaque_color_pass = id;
    }
    // Create Sky Pass
    {
      const uint32_t attachment_count = 2;
      VkAttachmentDescription attachments[attachment_count] = {
          {
              .format = self->render_system->render_thread->swapchain.format,
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
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = attachment_count,
          .pAttachments = attachments,
          .subpassCount = 1,
          .pSubpasses =
              &(VkSubpassDescription){
                  .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                  .colorAttachmentCount = 1,
                  .pColorAttachments =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      },
                  .pDepthStencilAttachment =
                      &(VkAttachmentReference){
                          1,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              },
      };
      TbRenderPassId id = create_render_pass(
          self, &create_info, 2,
          (TbRenderPassId[2]){self->opaque_depth_pass, self->opaque_color_pass},
          2, (TbRenderTargetId[2]){color_target, opaque_depth}, "Sky Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId, "Failed to create sky pass",
                      false);
      self->sky_pass = id;
    }
    // Create opaque depth copy pass
    {
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_R32_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              },
          .subpassCount = 1,
          .pSubpasses =
              &(VkSubpassDescription){
                  .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                  .colorAttachmentCount = 1,
                  .pColorAttachments =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              },
      };
      TbRenderPassId id =
          create_render_pass(self, &create_info, 1, &self->opaque_depth_pass, 1,
                             &depth_copy, "Depth Copy Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create depth copy pass", false);
      self->depth_copy_pass = id;
    }
    // Create transparent depth pass
    {
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_D32_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .finalLayout =
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              },
          .subpassCount = 1,
          .pSubpasses =
              &(VkSubpassDescription){
                  .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                  .pDepthStencilAttachment =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              },
      };

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->transparent_depth_pass, 1,
          &transparent_depth, "Transparent Depth Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create transparent depth pass", false);
      self->transparent_depth_pass = id;
    }
    // Create transparent color pass
    {
      const uint32_t attachment_count = 2;
      VkAttachmentDescription attachments[attachment_count] = {
          {
              .format = self->render_system->render_thread->swapchain.format,
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
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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
                  .colorAttachmentCount = 1,
                  .pColorAttachments =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      },
                  .pDepthStencilAttachment =
                      &(VkAttachmentReference){
                          1,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
              },
      };
      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->transparent_color_pass, 2,
          (TbRenderTargetId[2]){color_target, transparent_depth},
          "Transparent Color Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create transparent color pass", false);
      self->transparent_color_pass = id;
    }
    // Create UI Pass
    {
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format =
                      self->render_system->render_thread->swapchain.format,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              },
          .subpassCount = 1,
          .pSubpasses =
              &(VkSubpassDescription){
                  .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                  .colorAttachmentCount = 1,
                  .pColorAttachments =
                      &(VkAttachmentReference){
                          0,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      },
              },
          .pDependencies =
              &(VkSubpassDependency){
                  .srcStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstStageMask =
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              },
      };
      TbRenderPassId id = create_render_pass(self, &create_info, 1,
                                             &self->transparent_color_pass, 1,
                                             &color_target, "UI Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId, "Failed to create ui pass",
                      false);
      self->ui_pass = id;
    }
  }

  // Calculate pass order
  self->pass_order =
      tb_alloc_nm_tp(self->std_alloc, self->pass_count, uint32_t);

  // Stack to keep track of what child ids we need to process next
  // Worst case size is the same as the size of all passes
  int32_t pass_stack_head = 0;
  TbRenderPassId *pass_stack =
      tb_alloc_nm_tp(self->tmp_alloc, self->pass_count, TbRenderPassId);

  uint32_t pass_order_idx = 0;
  TbRenderPassId current_id = self->opaque_depth_pass;
  while (pass_order_idx < self->pass_count) {
    // Ensure that all dependent passes have already been scheduled
    bool deps_met = true;
    {
      const RenderPass *pass = &self->render_passes[current_id];
      for (uint32_t dep_idx = 0; dep_idx < pass->dep_count; ++dep_idx) {
        bool dep_found = false;
        for (uint32_t i = 0; i < pass_order_idx + 1; ++i) {
          if (self->pass_order[i] == pass->deps[dep_idx]) {
            dep_found = true;
            break;
          }
        }
        if (!dep_found) {
          deps_met = false;
          break;
        }
      }
    }

    // If dependencies were met, add this pass to the order and push its
    // children onto the stack
    if (deps_met) {
      self->pass_order[pass_order_idx++] = current_id;

      // Find all passes that depend on this pass
      uint32_t child_pass_count = 0;
      for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
        const RenderPass *pass = &self->render_passes[pass_idx];
        for (uint32_t dep_idx = 0; dep_idx < pass->dep_count; ++dep_idx) {
          if (pass->deps[dep_idx] == current_id) {
            child_pass_count++;
            break;
          }
        }
      }

      // Push children onto the stack to evaluate next
      if (child_pass_count > 0) {
        for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
          const RenderPass *pass = &self->render_passes[pass_idx];
          for (uint32_t dep_idx = 0; dep_idx < pass->dep_count; ++dep_idx) {
            if (pass->deps[dep_idx] == current_id) {
              pass_stack[pass_stack_head++] = (TbRenderPassId)pass_idx;
              break;
            }
          }
        }
      }
    }

    // Pop the next child off the stack to process
    if (pass_stack_head > 0) {
      pass_stack_head--;
      TbRenderPassId prev_id = current_id;
      current_id = pass_stack[pass_stack_head];

      // If dependecies were not met, after we've selected the next pass to
      // process, push the one that just failed back onto the stack to try again
      // next time
      if (!deps_met) {
        pass_stack[pass_stack_head++] = prev_id;
      }
    } else {
      TB_CHECK(deps_met, "unexpected outcome");
    }
  }

  // Register passes in execution order

  for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
    const uint32_t idx = self->pass_order[pass_idx];
    register_pass(self, self->render_system->render_thread, idx);
  }

  return true;
}

void destroy_render_pipeline_system(RenderPipelineSystem *self) {
  // Clean up all render passes
  for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
    RenderPass *pass = &self->render_passes[pass_idx];
    tb_rnd_destroy_render_pass(self->render_system, pass->pass);
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      tb_rnd_destroy_framebuffer(self->render_system, pass->framebuffers[i]);
    }
  }
  tb_free(self->std_alloc, self->render_passes);
  tb_free(self->std_alloc, self->pass_order);

  *self = (RenderPipelineSystem){0};
}

void tick_render_pipeline_system(RenderPipelineSystem *self,
                                 const SystemInput *input, SystemOutput *output,
                                 float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(render_pipeline, RenderPipelineSystem,
                 RenderPipelineSystemDescriptor)

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc) {
  *desc = (SystemDescriptor){
      .name = "Render Pipeline",
      .size = sizeof(RenderPipelineSystem),
      .id = RenderPipelineSystemId,
      .desc = (InternalDescriptor)pipe_desc,
      .dep_count = 0,
      .system_dep_count = 2,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = RenderTargetSystemId,
      .create = tb_create_render_pipeline_system,
      .destroy = tb_destroy_render_pipeline_system,
      .tick = tb_tick_render_pipeline_system,
  };
}

void tb_render_pipeline_register_draw_context(
    RenderPipelineSystem *self, const DrawContextDescriptor *desc) {
  (void)self;
  (void)desc;
}

VkRenderPass tb_render_pipeline_get_pass(RenderPipelineSystem *self,
                                         TbRenderPassId pass) {
  if (pass >= self->pass_count) {
    TB_CHECK_RETURN(false, "Pass Id out of range", VK_NULL_HANDLE);
  }
  return self->render_passes[pass].pass;
}

TbRenderPassId tb_render_pipeline_get_ordered_pass(RenderPipelineSystem *self,
                                                   uint32_t idx) {
  if (idx >= self->pass_count) {
    TB_CHECK_RETURN(false, "Ordered pass index out of range",
                    InvalidRenderPassId);
  }
  return (TbRenderPassId)self->pass_order[idx];
}

const VkFramebuffer *
tb_render_pipeline_get_pass_framebuffers(RenderPipelineSystem *self,
                                         TbRenderPassId pass) {
  if (pass >= self->pass_count) {
    TB_CHECK_RETURN(false, "Pass Id out of range", VK_NULL_HANDLE);
  }
  return self->render_passes[pass].framebuffers;
}
