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

#define MAX_RENDER_PASS_DEPS 4
#define MAX_RENDER_PASS_TRANS 4
#define MAX_RENDER_PASS_ATTACH 4

typedef struct PassTransition {
  TbRenderTargetId render_target;
  ImageTransition barrier;
} PassTransition;

typedef struct RenderPass {
  uint32_t dep_count;
  TbRenderPassId deps[MAX_RENDER_PASS_DEPS];

  uint32_t transition_count;
  PassTransition transitions[MAX_RENDER_PASS_TRANS];

  VkRenderPass pass;

  uint32_t attach_count;
  uint32_t attach_mips[MAX_RENDER_PASS_ATTACH];
  TbRenderTargetId attachments[MAX_RENDER_PASS_ATTACH];

  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];
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
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkViewport viewport;
  VkRect2D scissor;
  VkDescriptorSet set;
} FullscreenBatch;

VkResult create_depth_pipeline(RenderSystem *render_system, VkRenderPass pass,
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
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Depth Copy Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create depth copy pipeline", err);

  tb_rnd_destroy_shader(render_system, depth_vert_mod);
  tb_rnd_destroy_shader(render_system, depth_frag_mod);

  return err;
}

VkResult create_color_copy_pipeline(RenderSystem *render_system,
                                    VkRenderPass pass,
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
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Color Copy Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create color copy pipeline", err);

  tb_rnd_destroy_shader(render_system, color_vert_mod);
  tb_rnd_destroy_shader(render_system, color_frag_mod);

  return err;
}

VkResult create_tonemapping_pipeline(RenderSystem *render_system,
                                     VkRenderPass pass,
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
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Tonmapping Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create tonemapping pipeline", err);

  tb_rnd_destroy_shader(render_system, tonemap_vert_mod);
  tb_rnd_destroy_shader(render_system, tonemap_frag_mod);

  return err;
}

void record_fullscreen(VkCommandBuffer buffer, const FullscreenBatch *batch) {
  VkPipelineLayout layout = batch->layout;

  vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

  vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
  vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

  vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                          &batch->set, 0, NULL);

  // Just drawing a fullscreen triangle that's generated by the vertex shader
  vkCmdDraw(buffer, 3, 1, 0, 0);
}

void record_depth_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Depth Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Depth Copy", 1, true);
  cmd_begin_label(buffer, "Depth Copy", (float4){0.8f, 0.0f, 0.4f, 1.0f});

  // Only expecting one draw per pass
  if (batch_count != 1) {
    TracyCZoneEnd(ctx);
    return;
  }

  const FullscreenBatch *batch = (const FullscreenBatch *)batches;
  record_fullscreen(buffer, batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_color_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Color Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Color Copy", 1, true);
  cmd_begin_label(buffer, "Color Copy", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  // Only expecting one draw per pass
  if (batch_count != 1) {
    TracyCZoneEnd(ctx);
    return;
  }

  const FullscreenBatch *batch = (const FullscreenBatch *)batches;
  record_fullscreen(buffer, batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_tonemapping(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Tonemapping Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Tonemapping", 1, true);
  cmd_begin_label(buffer, "Tonemapping", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  // Only expecting one draw per pass
  if (batch_count != 1) {
    TracyCZoneEnd(ctx);
    return;
  }

  const FullscreenBatch *batch = (const FullscreenBatch *)batches;
  record_fullscreen(buffer, batch);

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

    VkFramebuffer framebuffer = pass->framebuffers[frame_idx];
    TbRenderTargetId target_id = pass->attachments[0];
    VkExtent3D target_ext = tb_render_target_get_mip_extent(
        self->render_target_system, pass->attach_mips[0], target_id);

    PassContext *pass_context = &state->pass_contexts[state->pass_ctx_count];

    *pass_context = (PassContext){
        .id = id,
        .command_buffer_index = command_buffers[id],
        .pass = pass->pass,
        .attachment_count = pass->attach_count,
        .barrier_count = pass->transition_count,
        .framebuffer = framebuffer,
        .width = target_ext.width,
        .height = target_ext.height,
    };

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

TbRenderPassId
create_render_pass(RenderPipelineSystem *self,
                   const VkRenderPassCreateInfo *create_info,
                   uint32_t dep_count, const TbRenderTargetId *deps,
                   uint32_t trans_count, const PassTransition *transitions,
                   uint32_t attach_count, const uint32_t *attach_mips,
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
             sizeof(TbRenderPassId) * MAX_RENDER_PASS_DEPS);
  if (dep_count > 0) {
    SDL_memcpy(pass->deps, deps, sizeof(TbRenderPassId) * dep_count);
  }

  // Copy attachments
  pass->attach_count = attach_count;
  SDL_memset(pass->attachments, InvalidRenderTargetId,
             sizeof(TbRenderTargetId) * MAX_RENDER_PASS_ATTACH);
  if (attach_count > 0) {
    SDL_memcpy(pass->attach_mips, attach_mips, sizeof(uint32_t) * attach_count);
    SDL_memcpy(pass->attachments, attachments,
               sizeof(TbRenderTargetId) * attach_count);
  }

  // Copy pass transitions
  pass->transition_count = trans_count;
  SDL_memset(pass->transitions, 0,
             sizeof(PassTransition) * MAX_RENDER_PASS_TRANS);
  if (trans_count > 0) {
    SDL_memcpy(pass->transitions, transitions,
               sizeof(PassTransition) * trans_count);
  }

  // Create framebuffers for render target based on attachments
  {
    RenderTargetSystem *rt_sys = self->render_target_system;
    // HACK: Assume all attachments have the same extents
    const VkExtent3D extent =
        tb_render_target_get_mip_extent(rt_sys, attach_mips[0], attachments[0]);
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      VkImageView attach_views[MAX_RENDER_PASS_ATTACH] = {0};

      for (uint32_t attach_idx = 0; attach_idx < attach_count; ++attach_idx) {
        attach_views[attach_idx] = tb_render_target_get_mip_view(
            rt_sys, attach_mips[attach_idx], i, attachments[attach_idx]);
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
    const TbRenderTargetId env_cube = render_target_system->env_cube;
    const TbRenderTargetId irradiance_map =
        render_target_system->irradiance_map;
    const TbRenderTargetId prefiltered_cube =
        render_target_system->prefiltered_cube;
    const TbRenderTargetId opaque_depth = render_target_system->depth_buffer;
    const TbRenderTargetId hdr_color = render_target_system->hdr_color;
    const TbRenderTargetId depth_copy = render_target_system->depth_buffer_copy;
    const TbRenderTargetId color_copy = render_target_system->color_copy;
    const TbRenderTargetId swapchain_target = render_target_system->swapchain;
    const TbRenderTargetId transparent_depth =
        render_target_system->depth_buffer;

    const uint32_t default_mip = 0;

    // Create opaque depth pass
    {
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_D32_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
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
      };

      TbRenderPassId id =
          create_render_pass(self, &create_info, 0, NULL, 0, NULL, 1,
                             &default_mip, &opaque_depth, "Opaque Depth Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create opaque depth pass", false);
      self->opaque_depth_pass = id;
    }
    // Create env capture pass
    {
      // https://blog.anishbhobe.site/vulkan-render-to-cubemaps-using-multiview/
      const uint32_t view_mask = 0x0000003F; // 0b00111111
      const uint32_t correlation_mask = 0;

      VkRenderPassMultiviewCreateInfo multiview_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO,
          .subpassCount = 1,
          .pViewMasks = &view_mask,
          .correlationMaskCount = 1,
          .pCorrelationMasks = &correlation_mask,
      };
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .pNext = &multiview_info,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
      };

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->opaque_depth_pass, 0, NULL, 1,
          &default_mip, &env_cube, "Env Capture Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create env capture pass", false);
      self->env_capture_pass = id;
    }
    // Create irradiance convolution pass
    {
      // https://blog.anishbhobe.site/vulkan-render-to-cubemaps-using-multiview/
      const uint32_t view_mask = 0x0000003F; // 0b00111111
      const uint32_t correlation_mask = 0;

      VkRenderPassMultiviewCreateInfo multiview_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO,
          .subpassCount = 1,
          .pViewMasks = &view_mask,
          .correlationMaskCount = 1,
          .pCorrelationMasks = &correlation_mask,
      };
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .pNext = &multiview_info,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
      };

      // Need to read the environment map
      PassTransition transition = {
          .render_target = env_cube,
          .barrier =
              {
                  .src_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  .barrier =
                      {
                          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                          .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                          .subresourceRange =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 6,
                              },
                      },
              },
      };

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->env_capture_pass, 1, &transition, 1,
          &default_mip, &irradiance_map, "Irradiance Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create irradiance pass", false);
      self->irradiance_pass = id;
    }
    // Create environment prefiltering passes
    {
      // https://blog.anishbhobe.site/vulkan-render-to-cubemaps-using-multiview/
      const uint32_t view_mask = 0x0000003F; // 0b00111111
      const uint32_t correlation_mask = 0;

      VkRenderPassMultiviewCreateInfo multiview_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO,
          .subpassCount = 1,
          .pViewMasks = &view_mask,
          .correlationMaskCount = 1,
          .pCorrelationMasks = &correlation_mask,
      };
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .pNext = &multiview_info,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
      };

      for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
        TbRenderPassId id = create_render_pass(
            self, &create_info, 1, &self->env_capture_pass, 0, NULL, 1, &i,
            &prefiltered_cube, "Prefilter Pass");
        TB_CHECK_RETURN(id != InvalidRenderPassId,
                        "Failed to create prefilter pass", false);
        self->prefilter_passes[i] = id;
      }
    }
    // Create opaque color pass
    {
#define ATTACH_COUNT 2
      VkAttachmentDescription attachments[ATTACH_COUNT] = {
          {
              .format = VK_FORMAT_R16G16B16A16_SFLOAT,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
          .attachmentCount = ATTACH_COUNT,
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
      };
#undef ATTACH_COUNT

      // Transition irradiance map and prefiltered env map
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
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
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
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = PREFILTER_PASS_COUNT,
                              .layerCount = 6,
                          },
                  },
          }};

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->opaque_depth_pass, 2,
          (PassTransition[2]){irr_trans, filter_trans}, 2,
          (uint32_t[2]){default_mip, default_mip},
          (TbRenderTargetId[2]){hdr_color, opaque_depth}, "Opaque Color Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create opaque color pass", false);
      self->opaque_color_pass = id;
    }
    // Create Sky Pass
    {
#define ATTACH_COUNT 2
      VkAttachmentDescription attachments[ATTACH_COUNT] = {
          {
              .format = VK_FORMAT_R16G16B16A16_SFLOAT,
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
          .attachmentCount = ATTACH_COUNT,
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
          // We want the depth buffer to be read by a shader in the next pass
          .dependencyCount = 2,
          .pDependencies =
              (VkSubpassDependency[2]){
                  {
                      .srcSubpass = VK_SUBPASS_EXTERNAL,
                      .dstSubpass = 0,
                      .srcStageMask =
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                      .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .srcAccessMask =
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                  },
                  {
                      .srcSubpass = 0,
                      .dstSubpass = VK_SUBPASS_EXTERNAL,
                      .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .dstStageMask =
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                      .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .dstAccessMask =
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                  },
              },
      };
#undef ATTACH_COUNT
      TbRenderPassId id = create_render_pass(
          self, &create_info, 2,
          (TbRenderPassId[2]){self->opaque_depth_pass, self->opaque_color_pass},
          0, NULL, 2, (uint32_t[2]){default_mip, default_mip},
          (TbRenderTargetId[2]){hdr_color, opaque_depth}, "Sky Pass");
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
      };

      PassTransition transition = {
          .render_target = self->render_target_system->depth_buffer,
          .barrier = {
              .src_flags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
              .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .barrier =
                  {
                      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask =
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .oldLayout =
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                              .levelCount = 1,
                              .layerCount = 1,
                          },
                  },
          }};

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->depth_copy_pass, 1, &transition, 1,
          &default_mip, &depth_copy, "Depth Copy Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create depth copy pass", false);
      self->depth_copy_pass = id;
    }
    // Create opaque color copy pass
    {
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments =
              &(VkAttachmentDescription){
                  .format =
                      self->render_system->render_thread->swapchain.format,
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
      };

      PassTransition transition = {
          .render_target = self->render_target_system->hdr_color,
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
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = 1,
                              .layerCount = 1,
                          },
                  },
          }};

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->color_copy_pass, 1, &transition, 1,
          &default_mip, &color_copy, "Color Copy Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create color copy pass", false);
      self->color_copy_pass = id;
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
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
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
          // We need to re-use the depth buffer as a depth buffer after the
          // depth copy
          .dependencyCount = 1,
          .pDependencies =
              (VkSubpassDependency[1]){
                  {
                      .srcSubpass = VK_SUBPASS_EXTERNAL,
                      .dstSubpass = 0,
                      .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .dstStageMask =
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                      .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .dstAccessMask =
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                      .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                  },
              },
      };

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->transparent_depth_pass, 0, NULL, 1,
          &default_mip, &transparent_depth, "Transparent Depth Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create transparent depth pass", false);
      self->transparent_depth_pass = id;
    }
    // Create transparent color pass
    {
#define ATTACH_COUNT 2
      VkAttachmentDescription attachments[ATTACH_COUNT] = {
          {
              .format = VK_FORMAT_R16G16B16A16_SFLOAT,
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
          .attachmentCount = ATTACH_COUNT,
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
      };
#undef ATTACH_COUNT

      PassTransition transition = {
          .render_target = self->render_target_system->hdr_color,
          .barrier = {
              .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .dst_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .barrier =
                  {
                      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = 1,
                              .layerCount = 1,
                          },
                  },
          }};

      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->transparent_color_pass, 1, &transition,
          2, (uint32_t[2]){default_mip, default_mip},
          (TbRenderTargetId[2]){hdr_color, transparent_depth},
          "Transparent Color Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create transparent color pass", false);
      self->transparent_color_pass = id;
    }
    // Create Tonemapping pass
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
      };
      PassTransition transition = {
          .render_target = self->render_target_system->hdr_color,
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
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = 1,
                              .layerCount = 1,
                          },
                  },
          }};
      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->transparent_color_pass, 1, &transition,
          1, &default_mip, &swapchain_target, "Tonemap Pass");
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create tonemap pass", false);
      self->tonemap_pass = id;
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
      };
      PassTransition transition = {
          .render_target = self->render_target_system->hdr_color,
          .barrier = {
              .src_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .dst_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .barrier =
                  {
                      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                      .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                      .subresourceRange =
                          {
                              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .levelCount = 1,
                              .layerCount = 1,
                          },
                  },
          }};
      TbRenderPassId id = create_render_pass(
          self, &create_info, 1, &self->tonemap_pass, 1, &transition, 1,
          &default_mip, &swapchain_target, "UI Pass");
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
          if (trans->barrier.src_flags < current_pass_flags ||
              trans->barrier.src_flags > trans->barrier.dst_flags) {
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
    {
      {
        VkSamplerCreateInfo create_info = {
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
        TB_VK_CHECK_RET(
            err, "Failed to create depth copy descriptor set layout", false);
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

      err = create_depth_pipeline(
          self->render_system, self->render_passes[self->depth_copy_pass].pass,
          self->copy_pipe_layout, &self->depth_copy_pipe);
      TB_VK_CHECK_RET(err, "Failed to create depth copy pipeline", false);

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
      err = create_color_copy_pipeline(
          self->render_system, self->render_passes[self->color_copy_pass].pass,
          self->copy_pipe_layout, &self->color_copy_pipe);
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
      err = create_tonemapping_pipeline(
          self->render_system, self->render_passes[self->color_copy_pass].pass,
          self->copy_pipe_layout, &self->tonemap_pipe);
      TB_VK_CHECK_RET(err, "Failed to create tonemapping pipeline", false);

      {
        DrawContextDescriptor desc = {
            .batch_size = sizeof(FullscreenBatch),
            .draw_fn = record_tonemapping,
            .pass_id = self->tonemap_pass,
        };
        self->tonemap_ctx =
            tb_render_pipeline_register_draw_context(self, &desc);
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

  // Clean up all render passes
  for (uint32_t pass_idx = 0; pass_idx < self->pass_count; ++pass_idx) {
    RenderPass *pass = &self->render_passes[pass_idx];
    tb_rnd_destroy_render_pass(self->render_system, pass->pass);
    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      tb_rnd_destroy_framebuffer(self->render_system, pass->framebuffers[i]);
    }
  }

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->render_system,
                                   self->descriptor_pools[i].set_pool);
  }

  tb_free(self->std_alloc, self->render_passes);
  tb_free(self->std_alloc, self->pass_order);

  *self = (RenderPipelineSystem){0};
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
      FullscreenBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->depth_copy_pipe,
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .set = depth_set,
      };
      tb_render_pipeline_issue_draw_batch(self, self->depth_copy_ctx, 1,
                                          &batch);
    }
    // Color copy pass
    {
      FullscreenBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->color_copy_pipe,
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .set = color_set,
      };
      tb_render_pipeline_issue_draw_batch(self, self->color_copy_ctx, 1,
                                          &batch);
    }
    // Tonemapping pass
    {
      FullscreenBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->tonemap_pipe,
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .set = color_set,
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
        .batch_size = desc->batch_size,
        .record_fn = desc->draw_fn,
    };
  }
  return id;
}

VkRenderPass tb_render_pipeline_get_pass(RenderPipelineSystem *self,
                                         TbRenderPassId pass) {
  TB_CHECK_RETURN(pass < self->pass_count, "Pass Id out of range",
                  VK_NULL_HANDLE);

  return self->render_passes[pass].pass;
}

const VkFramebuffer *
tb_render_pipeline_get_pass_framebuffers(RenderPipelineSystem *self,
                                         TbRenderPassId pass) {
  TB_CHECK_RETURN(pass < self->pass_count, "Pass Id out of range",
                  VK_NULL_HANDLE);

  return self->render_passes[pass].framebuffers;
}

void tb_render_pipeline_issue_draw_batch(RenderPipelineSystem *self,
                                         TbDrawContextId draw_ctx,
                                         uint32_t batch_count,
                                         const void *batches) {
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
        tb_realloc(self->std_alloc, ctx->batches, new_max * ctx->batch_size);
    ctx->batch_max = new_max;
  }

  // Copy batches into frame state's batch list
  void *dst = ((uint8_t *)ctx->batches) + (write_head * ctx->batch_size);
  SDL_memcpy(dst, batches, batch_count * ctx->batch_size);

  ctx->batch_count = new_count;
}
