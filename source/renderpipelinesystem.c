#include "renderpipelinesystem.h"

#include "profiling.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "world.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "colorcopy_frag.h"
#include "colorcopy_vert.h"
#include "depthcopy_frag.h"
#include "depthcopy_vert.h"
#include "tonemap_frag.h"
#include "tonemap_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

typedef struct PassTransition {
  TbRenderTargetId render_target;
  ImageTransition barrier;
} PassTransition;

typedef struct RenderPass {
  uint32_t dep_count;
  TbRenderPassId deps[TB_MAX_RENDER_PASS_DEPS];

  uint32_t transition_count;
  PassTransition transitions[TB_MAX_RENDER_PASS_TRANS];

  uint32_t attach_count;
  PassAttachment attachments[TB_MAX_ATTACHMENTS];

  VkRenderingInfo info[TB_MAX_FRAME_STATES];

#ifdef TRACY_ENABLE
  char label[TB_RP_LABEL_LEN];
#endif
} RenderPass;

// For dependency graph construction
typedef struct PassNode PassNode;
typedef struct PassNode {
  TbRenderPassId id;
  uint32_t child_count;
  PassNode **children;
} PassNode;

void sort_passes_recursive(PassNode *node, uint32_t *pass_order,
                           uint32_t *pass_idx) {
  if (node) {
    // Make sure id isn't already in the order
    bool exists = false;
    for (uint32_t i = 0; i < *pass_idx; ++i) {
      if (pass_order[i] == node->id) {
        exists = true;
        break;
      }
    }

    if (!exists) {
      pass_order[(*pass_idx)++] = (uint32_t)node->id;
    }

    for (uint32_t child_idx = 0; child_idx < node->child_count; ++child_idx) {
      sort_passes_recursive(node->children[child_idx], pass_order, pass_idx);
    }
  }
}

void sort_pass_graph(RenderPipelineSystem *self) {
  // Build a graph of pass nodes to determine ordering
  PassNode *nodes = tb_alloc_nm_tp(self->tmp_alloc, self->pass_count, PassNode);
  // All nodes have worst case pass_count children
  for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
    nodes[pass_idx].id = (TbRenderPassId)pass_idx;
    nodes[pass_idx].children =
        tb_alloc_nm_tp(self->tmp_alloc, self->pass_count, PassNode *);
  }

  // Build graph
  for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
    TbRenderPassId pass_id = (TbRenderPassId)pass_idx;
    PassNode *node = &nodes[pass_idx];

    // Search all other passes for children
    for (uint32_t i = 0; i < self->pass_count; ++i) {
      const RenderPass *pass = &self->render_passes[i];
      if (i != pass_idx) {
        for (uint32_t dep_idx = 0; dep_idx < pass->dep_count; ++dep_idx) {
          if (pass->deps[dep_idx] == pass_id) {
            node->children[node->child_count++] = &nodes[i];
            break;
          }
        }
      }
    }
  }

  // A pre-order traversal of the graph should get us a reasonable pass order
  uint32_t pass_idx = 0;
  sort_passes_recursive(&nodes[0], self->pass_order, &pass_idx);
}

// For interally driven passes
typedef struct FullscreenBatch {
  VkDescriptorSet set;
} FullscreenBatch;

VkResult create_depth_pipeline(RenderSystem *render_system,
                               VkFormat depth_format,
                               VkPipelineLayout pipe_layout,
                               VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule depth_vert_mod = VK_NULL_HANDLE;
  VkShaderModule depth_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(depthcopy_vert);
    create_info.pCode = (const uint32_t *)depthcopy_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Depth Copy Vert",
                               &depth_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load depth copy vert shader module", err);

    create_info.codeSize = sizeof(depthcopy_frag);
    create_info.pCode = (const uint32_t *)depthcopy_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Depth Copy Frag",
                               &depth_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load depth copy frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){depth_format},
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = depth_vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = depth_frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          },
      .pInputAssemblyState =
          &(VkPipelineInputAssemblyStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
              .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          },
      .pViewportState =
          &(VkPipelineViewportStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
              .viewportCount = 1,
              .pViewports = &(VkViewport){0, 600.0f, 800.0f, -600.0f, 0, 1},
              .scissorCount = 1,
              .pScissors = &(VkRect2D){{0, 0}, {800, 600}},
          },
      .pRasterizationState =
          &(VkPipelineRasterizationStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
              .polygonMode = VK_POLYGON_MODE_FILL,
              .cullMode = VK_CULL_MODE_NONE,
              .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
              .lineWidth = 1.0f,
          },
      .pMultisampleState =
          &(VkPipelineMultisampleStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
              .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments =
                  &(VkPipelineColorBlendAttachmentState){
                      .blendEnable = VK_FALSE,
                      .colorWriteMask =
                          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                  },
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = pipe_layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Depth Copy Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create depth copy pipeline", err);

  tb_rnd_destroy_shader(render_system, depth_vert_mod);
  tb_rnd_destroy_shader(render_system, depth_frag_mod);

  return err;
}

VkResult create_color_copy_pipeline(RenderSystem *render_system,
                                    VkFormat color_format,
                                    VkPipelineLayout pipe_layout,
                                    VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule color_vert_mod = VK_NULL_HANDLE;
  VkShaderModule color_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(colorcopy_vert);
    create_info.pCode = (const uint32_t *)colorcopy_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Color Copy Vert",
                               &color_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load color copy vert shader module", err);

    create_info.codeSize = sizeof(colorcopy_frag);
    create_info.pCode = (const uint32_t *)colorcopy_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Color Copy Frag",
                               &color_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load color copy frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = color_vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = color_frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          },
      .pInputAssemblyState =
          &(VkPipelineInputAssemblyStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
              .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          },
      .pViewportState =
          &(VkPipelineViewportStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
              .viewportCount = 1,
              .pViewports = &(VkViewport){0, 600.0f, 800.0f, -600.0f, 0, 1},
              .scissorCount = 1,
              .pScissors = &(VkRect2D){{0, 0}, {800, 600}},
          },
      .pRasterizationState =
          &(VkPipelineRasterizationStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
              .polygonMode = VK_POLYGON_MODE_FILL,
              .cullMode = VK_CULL_MODE_NONE,
              .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
              .lineWidth = 1.0f,
          },
      .pMultisampleState =
          &(VkPipelineMultisampleStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
              .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments =
                  &(VkPipelineColorBlendAttachmentState){
                      .blendEnable = VK_FALSE,
                      .colorWriteMask =
                          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                  },
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = pipe_layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Color Copy Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create color copy pipeline", err);

  tb_rnd_destroy_shader(render_system, color_vert_mod);
  tb_rnd_destroy_shader(render_system, color_frag_mod);

  return err;
}

VkResult create_tonemapping_pipeline(RenderSystem *render_system,
                                     VkFormat swap_target_format,
                                     VkPipelineLayout pipe_layout,
                                     VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule tonemap_vert_mod = VK_NULL_HANDLE;
  VkShaderModule tonemap_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(tonemap_vert);
    create_info.pCode = (const uint32_t *)tonemap_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Tonemapping Vert",
                               &tonemap_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load tonemapping vert shader module", err);

    create_info.codeSize = sizeof(tonemap_frag);
    create_info.pCode = (const uint32_t *)tonemap_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Tonemapping Frag",
                               &tonemap_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load tonemapping frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){swap_target_format},
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = tonemap_vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = tonemap_frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          },
      .pInputAssemblyState =
          &(VkPipelineInputAssemblyStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
              .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          },
      .pViewportState =
          &(VkPipelineViewportStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
              .viewportCount = 1,
              .pViewports = &(VkViewport){0, 600.0f, 800.0f, -600.0f, 0, 1},
              .scissorCount = 1,
              .pScissors = &(VkRect2D){{0, 0}, {800, 600}},
          },
      .pRasterizationState =
          &(VkPipelineRasterizationStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
              .polygonMode = VK_POLYGON_MODE_FILL,
              .cullMode = VK_CULL_MODE_NONE,
              .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
              .lineWidth = 1.0f,
          },
      .pMultisampleState =
          &(VkPipelineMultisampleStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
              .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments =
                  &(VkPipelineColorBlendAttachmentState){
                      .blendEnable = VK_FALSE,
                      .colorWriteMask =
                          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                  },
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = pipe_layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Tonmapping Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create tonemapping pipeline", err);

  tb_rnd_destroy_shader(render_system, tonemap_vert_mod);
  tb_rnd_destroy_shader(render_system, tonemap_frag_mod);

  return err;
}

void record_fullscreen(VkCommandBuffer buffer, const DrawBatch *batch,
                       const FullscreenBatch *fs_batch) {
  VkPipelineLayout layout = batch->layout;

  vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

  vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
  vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

  vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                          &fs_batch->set, 0, NULL);

  // Just drawing a fullscreen triangle that's generated by the vertex shader
  vkCmdDraw(buffer, 3, 1, 0, 0);
}

void record_depth_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const DrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Depth Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Depth Copy", 3, true);
  cmd_begin_label(buffer, "Depth Copy", (float4){0.8f, 0.0f, 0.4f, 1.0f});

  record_fullscreen(buffer, batches,
                    (const FullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_color_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const DrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Color Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Color Copy", 3, true);
  cmd_begin_label(buffer, "Color Copy", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  record_fullscreen(buffer, batches,
                    (const FullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_tonemapping(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Tonemapping Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Tonemapping", 3, true);
  cmd_begin_label(buffer, "Tonemapping", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  // Only expecting one draw per pass
  if (batch_count != 1) {
    TracyCZoneEnd(ctx);
    return;
  }

  record_fullscreen(buffer, batches,
                    (const FullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void register_pass(RenderPipelineSystem *self, RenderThread *thread,
                   TbRenderPassId id, uint32_t *command_buffers,
                   uint32_t command_buffer_count) {
  RenderPass *pass = &self->render_passes[id];
  Allocator std_alloc = self->std_alloc;
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state = &thread->frame_states[frame_idx];

    state->pass_command_buffer_count = command_buffer_count;
    {
      TB_CHECK(state->pass_command_buffer_count < TB_MAX_COMMAND_BUFFERS,
               "Too many command buffers");
      VkCommandBufferAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount =
              state->pass_command_buffer_count + 1, // HACK: +1 for the base
          .commandPool = state->command_pool,
      };
      VkResult err = vkAllocateCommandBuffers(thread->device, &alloc_info,
                                              state->pass_command_buffers);
      TB_VK_CHECK(err, "Failed to allocate pass command buffer");
      for (uint32_t i = 0; i < state->pass_command_buffer_count; ++i) {
        SET_VK_NAME(thread->device, state->pass_command_buffers[i],
                    VK_OBJECT_TYPE_COMMAND_BUFFER, "Pass Command Buffer");
      }
    }

    const uint32_t new_count = state->pass_ctx_count + 1;
    if (new_count > state->pass_ctx_max) {
      const uint32_t new_max = new_count * 2;
      state->pass_contexts = tb_realloc_nm_tp(std_alloc, state->pass_contexts,
                                              new_max, PassContext);
      state->pass_ctx_max = new_max;
    }

    TbRenderTargetId target_id = pass->attachments[0].attachment;
    VkExtent3D target_ext = tb_render_target_get_mip_extent(
        self->render_target_system, pass->attachments[0].mip, target_id);

    PassContext *pass_context = &state->pass_contexts[state->pass_ctx_count];
    TB_CHECK(pass->transition_count <= TB_MAX_BARRIERS, "Out of range");
    *pass_context = (PassContext){
        .id = id,
        .command_buffer_index = command_buffers[id],
        .attachment_count = pass->attach_count,
        .barrier_count = pass->transition_count,
        .width = target_ext.width,
        .height = target_ext.height,
        .render_info = &pass->info[frame_idx],
    };
    for (uint32_t i = 0; i < pass->attach_count; ++i) {
      pass_context->clear_values[i] = pass->attachments[i].clear_value;
    }
#ifdef TRACY_ENABLE
    SDL_strlcpy(pass_context->label, pass->label, TB_RP_LABEL_LEN);
#endif

    // Construct barriers
    for (uint32_t trans_idx = 0; trans_idx < pass->transition_count;
         ++trans_idx) {
      const PassTransition *transition = &pass->transitions[trans_idx];
      ImageTransition *barrier = &pass_context->barriers[trans_idx];
      *barrier = transition->barrier;
      barrier->barrier.image = tb_render_target_get_image(
          self->render_target_system, frame_idx, transition->render_target);
    }

    state->pass_ctx_count = new_count;
  }
}

typedef struct TbAttachmentInfo {
  VkClearValue clear_value;
  uint32_t mip;
  VkAttachmentLoadOp load_op;
  VkAttachmentStoreOp store_op;
  TbRenderTargetId attachment;
} TbAttachmentInfo;

typedef struct TbRenderPassCreateInfo {
  uint32_t view_mask;

  uint32_t dependency_count;
  const TbRenderPassId *dependencies;

  uint32_t transition_count;
  const PassTransition *transitions;

  uint32_t attachment_count;
  const TbAttachmentInfo *attachments;

  const char *name;
} TbRenderPassCreateInfo;

TbRenderPassId create_render_pass(RenderPipelineSystem *self,
                                  const TbRenderPassCreateInfo *create_info) {
  TB_CHECK_RETURN(create_info, "Invalid Create Info ptr", InvalidRenderPassId);

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

#ifdef TRACY_ENABLE
  if (create_info->name != NULL) {
    SDL_strlcpy(pass->label, create_info->name, TB_RP_LABEL_LEN);
  }
#endif

  // Copy dependencies
  pass->dep_count = create_info->dependency_count;
  SDL_memset(pass->deps, InvalidRenderPassId,
             sizeof(TbRenderPassId) * TB_MAX_RENDER_PASS_DEPS);
  if (pass->dep_count > 0) {
    SDL_memcpy(pass->deps, create_info->dependencies,
               sizeof(TbRenderPassId) * pass->dep_count);
  }

  // Copy attachments
  pass->attach_count = create_info->attachment_count;
  TB_CHECK_RETURN(pass->attach_count < TB_MAX_ATTACHMENTS, "Out of range",
                  InvalidRenderPassId);
  SDL_memset(pass->attachments, 0,
             sizeof(TbAttachmentInfo) * TB_MAX_ATTACHMENTS);
  for (uint32_t i = 0; i < pass->attach_count; ++i) {
    const TbAttachmentInfo *attach_info = &create_info->attachments[i];
    pass->attachments[i] = (PassAttachment){
        .clear_value = attach_info->clear_value,
        .mip = attach_info->mip,
        .attachment = attach_info->attachment,
    };
  }

  // Copy pass transitions
  pass->transition_count = create_info->transition_count;
  TB_CHECK_RETURN(pass->transition_count <= TB_MAX_RENDER_PASS_TRANS,
                  "Out of range", InvalidRenderPassId);
  SDL_memset(pass->transitions, 0,
             sizeof(PassTransition) * TB_MAX_RENDER_PASS_TRANS);
  if (pass->transition_count > 0) {
    SDL_memcpy(pass->transitions, create_info->transitions,
               sizeof(PassTransition) * pass->transition_count);
  }

  // Populate rendering info
  {
    RenderTargetSystem *rt_sys = self->render_target_system;
    // HACK: Assume all attachments have the same extents
    const VkExtent3D extent = tb_render_target_get_mip_extent(
        rt_sys, pass->attachments[0].mip, pass->attachments[0].attachment);

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      uint32_t color_count = 0;
      VkRenderingAttachmentInfo *depth_attachment = NULL;
      VkRenderingAttachmentInfo *stencil_attachment = NULL;
      VkRenderingAttachmentInfo *color_attachments = tb_alloc_nm_tp(
          self->std_alloc, TB_MAX_ATTACHMENTS, VkRenderingAttachmentInfo);

      for (uint32_t rt_idx = 0; rt_idx < pass->attach_count; ++rt_idx) {
        const TbAttachmentInfo *attachment = &create_info->attachments[rt_idx];
        VkFormat format = tb_render_target_get_format(
            self->render_target_system, attachment->attachment);
        VkImageView view = tb_render_target_get_mip_view(
            self->render_target_system, attachment->mip, i,
            attachment->attachment);

        VkRenderingAttachmentInfo *info = &color_attachments[color_count];
        VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (format == VK_FORMAT_D32_SFLOAT) {
          depth_attachment =
              tb_alloc_tp(self->std_alloc, VkRenderingAttachmentInfo);
          info = depth_attachment;
          layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        } else {
          color_count++;
        }
        *info = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = layout,
            .loadOp = attachment->load_op,
            .storeOp = attachment->store_op,
            .clearValue = attachment->clear_value,
        };
      }

      VkRenderingInfo *info = &pass->info[i];
      *info = (VkRenderingInfo){
          .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
          .renderArea = {.extent = {extent.width, extent.height}},
          .layerCount = 1,
          .viewMask = create_info->view_mask,
          .colorAttachmentCount = color_count,
          .pColorAttachments = color_attachments,
          .pDepthAttachment = depth_attachment,
          .pStencilAttachment = stencil_attachment,
      };
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
    const TbRenderTargetId env_cube = render_target_system->env_cube;
    const TbRenderTargetId irradiance_map =
        render_target_system->irradiance_map;
    const TbRenderTargetId prefiltered_cube =
        render_target_system->prefiltered_cube;
    const TbRenderTargetId opaque_depth = render_target_system->depth_buffer;
    const TbRenderTargetId opaque_normal = render_target_system->normal_buffer;
    const TbRenderTargetId hdr_color = render_target_system->hdr_color;
    const TbRenderTargetId depth_copy = render_target_system->depth_buffer_copy;
    const TbRenderTargetId color_copy = render_target_system->color_copy;
    const TbRenderTargetId swapchain_target = render_target_system->swapchain;
    const TbRenderTargetId transparent_depth =
        render_target_system->depth_buffer;
    const TbRenderTargetId *shadow_maps = render_target_system->shadow_maps;
    const TbRenderTargetId brightness_downsample =
        render_target_system->brightness_downsample;

    // Create opaque depth normal pass
    {
      TbRenderPassCreateInfo create_info = {
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  {
                      .render_target = self->render_target_system->depth_buffer,
                      .barrier =
                          {
                              .src_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_NONE,
                                      .dstAccessMask =
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_DEPTH_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
                  {
                      .render_target =
                          self->render_target_system->normal_buffer,
                      .barrier =
                          {
                              .src_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_NONE,
                                      .dstAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 2,
          .attachments =
              (TbAttachmentInfo[2]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = opaque_depth,
                  },
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = opaque_normal,
                  },
              },
          .name = "Opaque Depth Normal Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create opaque depth normal pass", false);
      self->opaque_depth_normal_pass = id;
    }

    // Create env capture pass
    {
      TbRenderPassCreateInfo create_info = {
          .view_mask = 0x0000003F, // 0b00111111
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->opaque_depth_normal_pass},
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
                  {
                      .render_target = self->render_target_system->env_cube,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_NONE,
                                      .dstAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 6,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = env_cube,
                  },
              },
          .name = "Env Capture Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create env capture pass", false);
      self->env_capture_pass = id;
    }
    // Create irradiance convolution pass
    {
      TbRenderPassCreateInfo create_info = {
          .view_mask = 0x0000003F, // 0b00111111
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->env_capture_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  {
                      .render_target = env_cube,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .oldLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 6,
                                          },
                                  },
                          },
                  },
                  {
                      .render_target =
                          self->render_target_system->irradiance_map,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_NONE,
                                      .dstAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 6,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = irradiance_map,
                  },
              },
          .name = "Irradiance Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create irradiance pass", false);
      self->irradiance_pass = id;
    }
    // Create environment prefiltering passes
    {
      for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
        uint32_t trans_count = 0;
        PassTransition transitions[1] = {0};

        // Do all mip transitions up-front
        if (i == 0) {
          trans_count = 1;
          transitions[0] = (PassTransition){
              .render_target = self->render_target_system->prefiltered_cube,
              .barrier =
                  {
                      .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .dst_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_NONE,
                              .dstAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                              .newLayout =
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .subresourceRange =
                                  {
                                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .levelCount = PREFILTER_PASS_COUNT,
                                      .layerCount = 6,
                                  },
                          },
                  },
          };
        }

        TbRenderPassCreateInfo create_info = {
            .view_mask = 0x0000003F, // 0b00111111
            .dependency_count = 1,
            .dependencies = (TbRenderPassId[1]){self->env_capture_pass},
            .transition_count = trans_count,
            .transitions = transitions,
            .attachment_count = 1,
            .attachments =
                (TbAttachmentInfo[1]){
                    {
                        .mip = i,
                        .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .attachment = prefiltered_cube,
                    },
                },
            .name = "Prefilter Pass",
        };

        TbRenderPassId id = create_render_pass(self, &create_info);
        TB_CHECK_RETURN(id != InvalidRenderPassId,
                        "Failed to create prefilter pass", false);
        self->prefilter_passes[i] = id;
      }
    }
    // Create shadow passes
    {
      // Note: this doesn't actually depend on the opaque depth pass,
      // but for now the pass dependencies system only has one starter node,
      // so everything must be a child of that
      for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
        // Front load all transitions on the first cascade
        uint32_t trans_count = 0;
        PassTransition transitions[TB_CASCADE_COUNT] = {0};
        if (i == 0) {
          for (uint32_t j = 0; j < TB_CASCADE_COUNT; ++j) {
            transitions[trans_count++] = (PassTransition){
                .render_target = self->render_target_system->shadow_maps[j],
                .barrier =
                    {
                        .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        .dst_flags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .barrier =
                            {
                                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                .srcAccessMask = VK_ACCESS_NONE,
                                .dstAccessMask =
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                .newLayout =
                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                .subresourceRange =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                        .levelCount = 1,
                                        .layerCount = 1,
                                    },
                            },
                    },
            };
          }
        }

        TbRenderPassCreateInfo create_info = {
            .dependency_count = 1,
            .dependencies = (TbRenderPassId[1]){self->opaque_depth_normal_pass},
            .transition_count = trans_count,
            .transitions = transitions,
            .attachment_count = 1,
            .attachments =
                (TbAttachmentInfo[1]){
                    {
                        .clear_value = {.depthStencil = {.depth = 1.0f,
                                                         .stencil = 0u}},
                        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .attachment = shadow_maps[i],
                    },
                },
            .name = "Shadow Pass",
        };

        TbRenderPassId id = create_render_pass(self, &create_info);
        TB_CHECK_RETURN(id != InvalidRenderPassId,
                        "Failed to create shadow pass", false);
        self->shadow_passes[i] = id;
      }
    }
    // Create opaque color pass
    {
      // Transition irradiance map, prefiltered env map and shadow map
      PassTransition irr_trans = {
          .render_target = self->render_target_system->irradiance_map,
          .barrier = {
              .src_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .barrier =
                  {
                      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = 1,
                              .layerCount = 6,
                          },
                  },
          }};
      PassTransition filter_trans = {
          .render_target = self->render_target_system->prefiltered_cube,
          .barrier = {
              .src_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .barrier =
                  {
                      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = PREFILTER_PASS_COUNT,
                              .layerCount = 6,
                          },
                  },
          }};
      PassTransition color_trans = {
          .render_target = self->render_target_system->hdr_color,
          .barrier =
              {
                  .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  .dst_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  .barrier =
                      {
                          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_NONE,
                          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .subresourceRange =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1,
                              },
                      },
              },
      };
      PassTransition normal_trans = {
          .render_target = self->render_target_system->normal_buffer,
          .barrier =
              {
                  .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  .dst_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  .barrier =
                      {
                          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_NONE,
                          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

                          .subresourceRange =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1,
                              },
                      },
              },
      };
      PassTransition shadow_trans_base = {
          .barrier = {
              .src_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .barrier =
                  {
                      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .oldLayout =
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                              .levelCount = 1,
                              .layerCount = 1,
                          },
                  },
          }};
      const uint32_t transition_count = TB_CASCADE_COUNT + 4;
      PassTransition transitions[transition_count] = {0};
      for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
        transitions[i] = shadow_trans_base;
        transitions[i].render_target = shadow_maps[i];
      }
      transitions[TB_CASCADE_COUNT + 0] = irr_trans;
      transitions[TB_CASCADE_COUNT + 1] = filter_trans;
      transitions[TB_CASCADE_COUNT + 2] = color_trans;
      transitions[TB_CASCADE_COUNT + 3] = normal_trans;

      TbRenderPassCreateInfo create_info = {
          .dependency_count = 2,
          .dependencies = (TbRenderPassId[2]){self->opaque_depth_normal_pass,
                                              self->shadow_passes[3]},
          .transition_count = transition_count,
          .transitions = transitions,
          .attachment_count = 2,
          .attachments =
              (TbAttachmentInfo[2]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = hdr_color,
                  },
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = opaque_depth,
                  },
              },
          .name = "Opaque Color Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create opaque color pass", false);
      self->opaque_color_pass = id;
    }
    // Create sky Pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 2,
          .dependencies = (TbRenderPassId[2]){self->opaque_depth_normal_pass,
                                              self->opaque_color_pass},
          .attachment_count = 2,
          .attachments =
              (TbAttachmentInfo[2]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = hdr_color,
                  },
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = opaque_depth,
                  },
              },
          .name = "Sky Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId, "Failed to create sky pass",
                      false);
      self->sky_pass = id;
    }
    // Create opaque depth copy pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->sky_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  {
                      .render_target = self->render_target_system->depth_buffer,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .oldLayout =
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_DEPTH_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
                  {
                      .render_target =
                          self->render_target_system->depth_buffer_copy,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = depth_copy,
                  },
              },
          .name = "Depth Copy Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create depth copy pass", false);
      self->depth_copy_pass = id;
    }
    // Create opaque color copy pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->depth_copy_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  {
                      .render_target = self->render_target_system->hdr_color,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .oldLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
                  {
                      .render_target = self->render_target_system->color_copy,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = color_copy,
                  },
              },
          .name = "Color Copy Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create color copy pass", false);
      self->color_copy_pass = id;
    }
    // Create transparent depth pass
    {
      // Must transition back to depth so that we can load the contents
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->color_copy_pass},
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
                  {
                      .render_target = self->render_target_system->depth_buffer,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                      .oldLayout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_DEPTH_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .attachment = transparent_depth,
                  },
              },
          .name = "Transparent Depth Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create transparent depth pass", false);
      self->transparent_depth_pass = id;
    }
    // Create transparent color pass
    {
      PassTransition transitions[3] = {
          {
              .render_target = self->render_target_system->hdr_color,
              .barrier =
                  {
                      .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .dst_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                              .dstAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .oldLayout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              .newLayout =
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .subresourceRange =
                                  {
                                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  },
                          },
                  },
          },
          {
              .render_target = self->render_target_system->color_copy,
              .barrier =
                  {
                      .src_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                              .oldLayout =
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .newLayout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              .subresourceRange =
                                  {
                                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  },
                          },
                  },
          },
          {
              .render_target = self->render_target_system->depth_buffer_copy,
              .barrier =
                  {
                      .src_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                              .oldLayout =
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .newLayout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              .subresourceRange =
                                  {
                                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  },
                          },
                  },
          },
      };

      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->transparent_depth_pass},
          .transition_count = 3,
          .transitions = transitions,
          .attachment_count = 2,
          .attachments =
              (TbAttachmentInfo[2]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = hdr_color,
                  },
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = transparent_depth,
                  },
              },
          .name = "Transparent Color Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create transparent color pass", false);
      self->transparent_color_pass = id;
    }
    // Create brightness pass
    {
      static const size_t trans_count = 2;
      PassTransition transitions[trans_count] = {
          {
              .render_target = self->render_target_system->hdr_color,
              .barrier =
                  {
                      .src_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                              .oldLayout =
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .newLayout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              .subresourceRange =
                                  {
                                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  },
                          },
                  },
          },
          {
              .render_target =
                  self->render_target_system->brightness_downsample,
              .barrier =
                  {
                      .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .dst_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_NONE,
                              .dstAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                              .newLayout =
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .subresourceRange =
                                  {
                                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .levelCount = 1,
                                      .layerCount = 1,
                                  },
                          },
                  },
          },
      };
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->transparent_color_pass},
          .transition_count = trans_count,
          .transitions = transitions,
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = brightness_downsample,
                  },
              },
          .name = "Brightness Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create brightness downsample pass", false);
      self->brightness_pass = id;
    }
    // Create bloom x pass
    {

    }  // Create bloom y pass
    {} // Create tonemapping pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->brightness_pass},
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = swapchain_target,
                  },
              },
          .name = "Tonemap Pass",
      };
      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create tonemap pass", false);
      self->tonemap_pass = id;
    }
    // Create UI Pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->tonemap_pass},
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
                  {
                      .render_target = self->render_target_system->hdr_color,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              .dst_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .oldLayout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .newLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = 1,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = swapchain_target,
                  },
              },
          .name = "UI Pass",
      };
      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId, "Failed to create ui pass",
                      false);
      self->ui_pass = id;
    }
  }

  // Calculate pass order
  self->pass_order =
      tb_alloc_nm_tp(self->std_alloc, self->pass_count, uint32_t);

  sort_pass_graph(self);

  // Once we've sorted passes, go through the passes
  // in execution order and determine where full pipelines are used.
  // Every time we return to the top of the pipeline, we want to keep track
  // so we can use a different command buffer.
  {
    uint32_t command_buffer_count = 0; // Treated as an index while builiding
    // Worst case each pass needs its own command buffer
    uint32_t *command_buffer_indices =
        tb_alloc_nm_tp(self->tmp_alloc, self->pass_count, uint32_t);

    {
      uint32_t current_pass_flags = 0;
      for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
        const uint32_t idx = self->pass_order[pass_idx];
        RenderPass *pass = &self->render_passes[idx];

        for (uint32_t trans_idx = 0; trans_idx < pass->transition_count;
             ++trans_idx) {
          PassTransition *trans = &pass->transitions[trans_idx];

          // If we can tell that the transition indicates a pipeline flush
          // we want to record that work onto a different command buffer so
          // we can submit previous work before continuing to record.
          // This way we can reduce GPU pipeline stalls
          if (idx > 0 && // But this is only possible if we're not on the 0th
                         // pass
              (trans->barrier.src_flags < current_pass_flags ||
               trans->barrier.src_flags > trans->barrier.dst_flags)) {
            command_buffer_count++;
          }

          // Either way, record that the pass flags are different
          current_pass_flags = trans->barrier.dst_flags;
        }

        command_buffer_indices[pass_idx] = command_buffer_count;
      }
      command_buffer_count++;

      // Register passes in execution order
      for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
        const uint32_t idx = self->pass_order[pass_idx];
        register_pass(self, self->render_system->render_thread, idx,
                      command_buffer_indices, command_buffer_count);
      }
    }
  }

  // Construct additional objects for handling draws that this system is
  // responsible for
  {
    VkResult err = VK_SUCCESS;

    // Depth Copy
    {{VkSamplerCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
          .magFilter = VK_FILTER_LINEAR,
          .minFilter = VK_FILTER_LINEAR,
          .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
          .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
          .anisotropyEnable = VK_FALSE,
          .maxAnisotropy = 1.0f,
          .maxLod = 1.0f,
          .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
      };
    err = tb_rnd_create_sampler(render_system, &create_info,
                                "Depth Copy Sampler", &self->sampler);
    TB_VK_CHECK_RET(err, "Failed to create depth copy sampler", err);
  }

  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = (VkDescriptorSetLayoutBinding[2]){
            {
                .binding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = &self->sampler,
            },
        }};
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "Depth Copy Descriptor Set Layout",
                                   &self->copy_set_layout);
    TB_VK_CHECK_RET(err, "Failed to create depth copy descriptor set layout",
                    false);
  }

  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts =
            (VkDescriptorSetLayout[1]){
                self->copy_set_layout,
            },
    };
    err = tb_rnd_create_pipeline_layout(self->render_system, &create_info,
                                        "Depth Copy Pipeline Layout",
                                        &self->copy_pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create depth pipeline layout", false);
  }

  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(self, self->depth_copy_pass,
                                       &attach_count, NULL);
    TB_CHECK_RETURN(attach_count == 1, "Unexpected", false);
    PassAttachment depth_info = {0};
    tb_render_pipeline_get_attachments(self, self->depth_copy_pass,
                                       &attach_count, &depth_info);

    VkFormat depth_format = tb_render_target_get_format(
        self->render_target_system, depth_info.attachment);

    err = create_depth_pipeline(self->render_system, depth_format,
                                self->copy_pipe_layout, &self->depth_copy_pipe);
    TB_VK_CHECK_RET(err, "Failed to create depth copy pipeline", false);
  }

  {
    DrawContextDescriptor desc = {
        .batch_size = sizeof(FullscreenBatch),
        .draw_fn = record_depth_copy,
        .pass_id = self->depth_copy_pass,
    };
    self->depth_copy_ctx =
        tb_render_pipeline_register_draw_context(self, &desc);
    TB_CHECK_RETURN(self->depth_copy_ctx != InvalidDrawContextId,
                    "Failed to create depth copy draw context", false);
  }
}

// Color Copy
{
  uint32_t attach_count = 0;
  tb_render_pipeline_get_attachments(self, self->color_copy_pass, &attach_count,
                                     NULL);
  TB_CHECK_RETURN(attach_count == 1, "Unexpected", false);
  PassAttachment attach_info = {0};
  tb_render_pipeline_get_attachments(self, self->color_copy_pass, &attach_count,
                                     &attach_info);

  VkFormat color_format = tb_render_target_get_format(
      self->render_target_system, attach_info.attachment);

  err = create_color_copy_pipeline(self->render_system, color_format,
                                   self->copy_pipe_layout,
                                   &self->color_copy_pipe);
  TB_VK_CHECK_RET(err, "Failed to create color copy pipeline", false);

  {
    DrawContextDescriptor desc = {
        .batch_size = sizeof(FullscreenBatch),
        .draw_fn = record_color_copy,
        .pass_id = self->color_copy_pass,
    };
    self->color_copy_ctx =
        tb_render_pipeline_register_draw_context(self, &desc);
    TB_CHECK_RETURN(self->color_copy_ctx != InvalidDrawContextId,
                    "Failed to create color copy draw context", false);
  }
}

// Tonemapping
{
  uint32_t attach_count = 0;
  tb_render_pipeline_get_attachments(self, self->tonemap_pass, &attach_count,
                                     NULL);
  TB_CHECK(attach_count == 1, "Unexpected");
  PassAttachment attach_info = {0};
  tb_render_pipeline_get_attachments(self, self->tonemap_pass, &attach_count,
                                     &attach_info);

  VkFormat swap_target_format = tb_render_target_get_format(
      self->render_target_system, attach_info.attachment);

  err =
      create_tonemapping_pipeline(self->render_system, swap_target_format,
                                  self->copy_pipe_layout, &self->tonemap_pipe);
  TB_VK_CHECK_RET(err, "Failed to create tonemapping pipeline", false);

  {
    DrawContextDescriptor desc = {
        .batch_size = sizeof(FullscreenBatch),
        .draw_fn = record_tonemapping,
        .pass_id = self->tonemap_pass,
    };
    self->tonemap_ctx = tb_render_pipeline_register_draw_context(self, &desc);
    TB_CHECK_RETURN(self->tonemap_ctx != InvalidDrawContextId,
                    "Failed to create tonemapping draw context", false);
  }
}
}

return true;
}

void destroy_render_pipeline_system(RenderPipelineSystem *self) {
  tb_rnd_destroy_sampler(self->render_system, self->sampler);
  tb_rnd_destroy_set_layout(self->render_system, self->copy_set_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->copy_pipe_layout);
  tb_rnd_destroy_pipeline(self->render_system, self->depth_copy_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->color_copy_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->tonemap_pipe);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->render_system,
                                   self->descriptor_pools[i].set_pool);
  }

  tb_free(self->std_alloc, self->render_passes);
  tb_free(self->std_alloc, self->pass_order);

  *self = (RenderPipelineSystem){0};
}

void reimport_render_pass(RenderPipelineSystem *self, TbRenderPassId id) {
  RenderPass *rp = &self->render_passes[id];

  {
    RenderTargetSystem *rt_sys = self->render_target_system;
    // HACK: Assume all attachments have the same extents
    const VkExtent3D extent = tb_render_target_get_mip_extent(
        rt_sys, rp->attachments[0].mip, rp->attachments[0].attachment);

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      // Update the pass context on each frame index
      {
        FrameState *state =
            &self->render_system->render_thread->frame_states[i];
        PassContext *context = &state->pass_contexts[id];
        context->width = extent.width;
        context->height = extent.height;

        context->render_info->renderArea.extent =
            (VkExtent2D){extent.width, extent.height};

        uint32_t col_count = 0;
        for (uint32_t attach_idx = 0; attach_idx < rp->attach_count;
             ++attach_idx) {
          TbRenderTargetId rt = rp->attachments[attach_idx].attachment;
          VkFormat format = tb_render_target_get_format(rt_sys, rt);
          VkImageView view = tb_render_target_get_mip_view(
              rt_sys, rp->attachments[attach_idx].mip, i, rt);

          // Forgive the const casting :(
          if (format == VK_FORMAT_D32_SFLOAT) {
            ((VkRenderingAttachmentInfo *)
                 context->render_info->pDepthAttachment)
                ->imageView = view;
          } else {
            ((VkRenderingAttachmentInfo *)
                 context->render_info->pColorAttachments)[col_count++]
                .imageView = view;
          }
        }
      }
    }
  }
}

void tb_rnd_on_swapchain_resize(RenderPipelineSystem *self) {
  // Called by the core system as a hack when the swapchain resizes
  // This is where, on the main thread, we have to adjust to any render passes
  // and render targets to stay up to date with the latest swapchain

  // Reimport the swapchain target and resize all default targets
  // The render thread should have created the necessary resources before
  // signaling the main thread
  tb_reimport_swapchain(self->render_target_system);

  // Render target system is up to date, now we just have to re-create all
  // render passes
  {
    reimport_render_pass(self, self->opaque_depth_normal_pass);
    reimport_render_pass(self, self->opaque_color_pass);
    reimport_render_pass(self, self->depth_copy_pass);
    reimport_render_pass(self, self->color_copy_pass);
    reimport_render_pass(self, self->sky_pass);
    reimport_render_pass(self, self->transparent_depth_pass);
    reimport_render_pass(self, self->transparent_color_pass);
    reimport_render_pass(self, self->tonemap_pass);
    reimport_render_pass(self, self->ui_pass);
  }

  // We now need to patch every pass's transitions so that their targets point
  // at the right VkImages
  for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
    RenderPass *pass = &self->render_passes[pass_idx];
    for (uint32_t trans_idx = 0; trans_idx < pass->transition_count;
         ++trans_idx) {
      for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES;
           ++frame_idx) {
        PassContext *context =
            &self->render_system->render_thread->frame_states[frame_idx]
                 .pass_contexts[pass_idx];
        const PassTransition *transition = &pass->transitions[trans_idx];
        ImageTransition *barrier = &context->barriers[trans_idx];
        *barrier = transition->barrier;
        barrier->barrier.image = tb_render_target_get_image(
            self->render_target_system, frame_idx, transition->render_target);
      }
    }
  }

  // Also clear out any draws that were in flight on the render thread
  // Any draws that had descriptors that point to these re-created resources
  // are invalid
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state =
        &self->render_system->render_thread->frame_states[frame_idx];
    for (uint32_t ctx_idx = 0; ctx_idx < state->draw_ctx_count; ++ctx_idx) {
      DrawContext *draw_ctx = &state->draw_contexts[ctx_idx];
      draw_ctx->batch_count = 0;
    }
  }
}

void tick_render_pipeline_system(RenderPipelineSystem *self,
                                 const SystemInput *input, SystemOutput *output,
                                 float delta_seconds) {
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "Render Pipeline System Tick", TracyCategoryColorRendering,
               true);

  const uint32_t frame_idx = self->render_system->frame_idx;

  // Clear all of this frame's draw batches
  FrameState *state =
      &self->render_system->render_thread->frame_states[frame_idx];

  for (uint32_t i = 0; i < state->draw_ctx_count; ++i) {
    state->draw_contexts[i].batch_count = 0;
  }

  // A few passes will be driven from here because an external system
  // has no need to directly drive these passes

  // Allocate and write all core descriptor sets
  {
    VkResult err = VK_SUCCESS;
    // Allocate the one known descriptor set we need for this frame
    {
      const uint32_t set_count = 2;
      VkDescriptorPoolCreateInfo pool_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = set_count * 2,
          .poolSizeCount = 1,
          .pPoolSizes =
              &(VkDescriptorPoolSize){
                  .descriptorCount = set_count * 2,
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              },
      };
      err = tb_rnd_frame_desc_pool_tick(self->render_system, &pool_info,
                                        self->copy_set_layout,
                                        self->descriptor_pools, set_count);
      TB_VK_CHECK(err, "Failed to tick descriptor pool");
    }

    VkDescriptorSet depth_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 0);
    VkDescriptorSet color_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 1);

    VkImageView depth_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->depth_buffer);
    VkImageView color_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->hdr_color);

// Write the descriptor set
#define WRITE_COUNT 2
    VkWriteDescriptorSet writes[WRITE_COUNT] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = depth_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .imageView = depth_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = color_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .imageView = color_view,
                },
        },
    };
    vkUpdateDescriptorSets(self->render_system->render_thread->device,
                           WRITE_COUNT, writes, 0, NULL);
#undef WRITE_COUNT
  }

  // Issue draws for full screen passes
  {
    VkDescriptorSet depth_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 0);
    VkDescriptorSet color_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 1);

    // TODO: Make this less hacky
    const uint32_t width = self->render_system->render_thread->swapchain.width;
    const uint32_t height =
        self->render_system->render_thread->swapchain.height;

    // Depth copy pass
    {
      FullscreenBatch fs_batch = {
          .set = depth_set,
      };
      DrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->depth_copy_pipe,
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->depth_copy_ctx, 1,
                                          &batch);
    }
    // Color copy pass
    {
      FullscreenBatch fs_batch = {
          .set = color_set,
      };
      DrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->color_copy_pipe,
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->color_copy_ctx, 1,
                                          &batch);
    }
    // Tonemapping pass
    {
      FullscreenBatch fs_batch = {
          .set = color_set,
      };
      DrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->tonemap_pipe,
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->tonemap_ctx, 1, &batch);
    }
  }

  TracyCZoneEnd(ctx);
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

TbDrawContextId
tb_render_pipeline_register_draw_context(RenderPipelineSystem *self,
                                         const DrawContextDescriptor *desc) {
  Allocator std_alloc = self->std_alloc;
  RenderThread *thread = self->render_system->render_thread;
  TbDrawContextId id = thread->frame_states[0].draw_ctx_count;
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state = &thread->frame_states[frame_idx];

    const uint32_t new_count = state->draw_ctx_count + 1;
    if (new_count > state->draw_ctx_max) {
      const uint32_t new_max = new_count * 2;
      state->draw_contexts = tb_realloc_nm_tp(std_alloc, state->draw_contexts,
                                              new_max, DrawContext);
      state->draw_ctx_max = new_max;
    }
    state->draw_ctx_count = new_count;

    state->draw_contexts[id] = (DrawContext){
        .pass_id = desc->pass_id,
        .user_batch_size = desc->batch_size,
        .record_fn = desc->draw_fn,
    };
  }
  return id;
}

void tb_render_pipeline_get_attachments(RenderPipelineSystem *self,
                                        TbRenderPassId pass,
                                        uint32_t *attach_count,
                                        PassAttachment *attachments) {
  TB_CHECK(pass < self->pass_count, "Pass Id out of range");
  TB_CHECK(attach_count, "Attachment count pointer must be valid");
  TB_CHECK(*attach_count <= TB_MAX_ATTACHMENTS, "Too many attachments");

  const RenderPass *p = &self->render_passes[pass];

  if (attachments == NULL) {
    // Attachments ptr was not specified, set the attachment count and return
    *attach_count = p->attach_count;
    return;
  } else {
    // Attachment count and attachment pointers were provided
    TB_CHECK(*attach_count == p->attach_count, "Unexpected size mismatch");
    SDL_memcpy(attachments, p->attachments,
               sizeof(PassAttachment) * (*attach_count));
  }
}

void tb_render_pipeline_issue_draw_batch(RenderPipelineSystem *self,
                                         TbDrawContextId draw_ctx,
                                         uint32_t batch_count,
                                         const DrawBatch *batches) {
  RenderThread *thread = self->render_system->render_thread;
  FrameState *state = &thread->frame_states[self->render_system->frame_idx];
  if (draw_ctx >= state->draw_ctx_count) {
    TB_CHECK(false, "Draw Context Id out of range");
    return;
  }

  DrawContext *ctx = &state->draw_contexts[draw_ctx];

  const uint32_t write_head = ctx->batch_count;
  const uint32_t new_count = ctx->batch_count + batch_count;
  if (new_count > ctx->batch_max) {
    const uint32_t new_max = new_count * 2;
    ctx->batches =
        tb_realloc_nm_tp(self->std_alloc, ctx->batches, new_max, DrawBatch);
    ctx->user_batches = tb_realloc(self->std_alloc, ctx->user_batches,
                                   new_max * ctx->user_batch_size);
    ctx->batch_max = new_max;
  }

  // Copy batches into frame state's batch list
  DrawBatch *dst = &ctx->batches[write_head];
  SDL_memcpy(dst, batches, batch_count * sizeof(DrawBatch));

  for (uint32_t i = 0; i < 0 + batch_count; ++i) {
    void *user_dst = ((uint8_t *)ctx->user_batches) +
                     ((i + write_head) * ctx->user_batch_size);
    SDL_memcpy(user_dst, batches[i].user_batch, ctx->user_batch_size);
    ctx->batches[i + write_head].user_batch = user_dst;
  }

  ctx->batch_count = new_count;
}
