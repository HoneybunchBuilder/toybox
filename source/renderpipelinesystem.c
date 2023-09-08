#include "renderpipelinesystem.h"

#include "profiling.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "shadercommon.h"
#include "ssao.h"
#include "tbcommon.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "blur_h_comp.h"
#include "blur_v_comp.h"
#include "brightness_frag.h"
#include "brightness_vert.h"
#include "colorcopy_frag.h"
#include "colorcopy_vert.h"
#include "copy_comp.h"
#include "depthcopy_frag.h"
#include "depthcopy_vert.h"
#include "ssao_frag.h"
#include "ssao_vert.h"
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
  const uint32_t pass_count = TB_DYN_ARR_SIZE(self->render_passes);
  PassNode *nodes = tb_alloc_nm_tp(self->tmp_alloc, pass_count, PassNode);
  // All nodes have worst case pass_count children
  for (uint32_t pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
    nodes[pass_idx].id = (TbRenderPassId)pass_idx;
    nodes[pass_idx].children =
        tb_alloc_nm_tp(self->tmp_alloc, pass_count, PassNode *);
  }

  // Build graph
  for (uint32_t pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
    TbRenderPassId pass_id = (TbRenderPassId)pass_idx;
    PassNode *node = &nodes[pass_idx];

    // Search all other passes for children
    for (uint32_t i = 0; i < pass_count; ++i) {
      const RenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, i);
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

typedef struct SSAOBatch {
  VkDescriptorSet ssao_set;
  VkDescriptorSet view_set;
  SSAOPushConstants consts;
} SSAOBatch;

typedef struct BlurBatch {
  VkDescriptorSet set;
} BlurBatch;

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

VkResult create_ssao_pipeline(RenderSystem *render_system,
                              VkFormat color_format,
                              VkPipelineLayout pipe_layout,
                              VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;
  VkShaderModule ssao_vert_mod = VK_NULL_HANDLE;
  VkShaderModule ssao_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(ssao_vert);
    create_info.pCode = (const uint32_t *)ssao_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "ssao Vert",
                               &ssao_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load ssao vert shader module", err);

    create_info.codeSize = sizeof(ssao_frag);
    create_info.pCode = (const uint32_t *)ssao_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "ssao Frag",
                               &ssao_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load ssao frag shader module", err);
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
                  .module = ssao_vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = ssao_frag_mod,
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
                                         "ssao Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ssao pipeline", err);

  tb_rnd_destroy_shader(render_system, ssao_vert_mod);
  tb_rnd_destroy_shader(render_system, ssao_frag_mod);

  return err;
}

VkResult create_comp_copy_pipeline(RenderSystem *render_system,
                                   VkPipelineLayout layout,
                                   VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;
  VkShaderModule copy_comp_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(copy_comp);
    create_info.pCode = (const uint32_t *)copy_comp;
    err = tb_rnd_create_shader(render_system, &create_info, "Copy Comp",
                               &copy_comp_mod);
    TB_VK_CHECK_RET(err, "Failed to load copy compute shader module", err);
  }

  VkComputePipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = copy_comp_mod,
              .pName = "comp",
          },
      .layout = layout,
  };
  err = tb_rnd_create_compute_pipelines(render_system, 1, &create_info,
                                        "Compute Copy Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create compute copy pipeline", err);

  tb_rnd_destroy_shader(render_system, copy_comp_mod);

  return err;
}

VkResult create_blur_pipelines(RenderSystem *render_system,
                               VkPipelineLayout layout, VkPipeline *h_pipe,
                               VkPipeline *v_pipe) {
  VkResult err = VK_SUCCESS;
  VkShaderModule blur_h_comp_mod = VK_NULL_HANDLE;
  VkShaderModule blur_v_comp_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(blur_h_comp);
    create_info.pCode = (const uint32_t *)blur_h_comp;
    err = tb_rnd_create_shader(render_system, &create_info, "Blur H Comp",
                               &blur_h_comp_mod);
    TB_VK_CHECK_RET(err, "Failed to load blur compute shader module", err);
  }

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(blur_v_comp);
    create_info.pCode = (const uint32_t *)blur_v_comp;
    err = tb_rnd_create_shader(render_system, &create_info, "Blur V Comp",
                               &blur_v_comp_mod);
    TB_VK_CHECK_RET(err, "Failed to load blur compute shader module", err);
  }

  {
    VkComputePipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            (VkPipelineShaderStageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = blur_h_comp_mod,
                .pName = "comp",
            },
        .layout = layout,
    };
    err = tb_rnd_create_compute_pipelines(render_system, 1, &create_info,
                                          "Blur H Pipeline", h_pipe);
    TB_VK_CHECK_RET(err, "Failed to create horizontal blur pipeline", err);
  }

  {
    VkComputePipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            (VkPipelineShaderStageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = blur_v_comp_mod,
                .pName = "comp",
            },
        .layout = layout,
    };
    err = tb_rnd_create_compute_pipelines(render_system, 1, &create_info,
                                          "Blur V Pipeline", v_pipe);
    TB_VK_CHECK_RET(err, "Failed to create vertical blur pipeline", err);
  }

  tb_rnd_destroy_shader(render_system, blur_h_comp_mod);
  tb_rnd_destroy_shader(render_system, blur_v_comp_mod);

  return err;
}

VkResult create_brightness_pipeline(RenderSystem *render_system,
                                    VkFormat color_format,
                                    VkPipelineLayout pipe_layout,
                                    VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;
  VkShaderModule brightness_vert_mod = VK_NULL_HANDLE;
  VkShaderModule brightness_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(brightness_vert);
    create_info.pCode = (const uint32_t *)brightness_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Brightness Vert",
                               &brightness_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load brightness vert shader module", err);

    create_info.codeSize = sizeof(brightness_frag);
    create_info.pCode = (const uint32_t *)brightness_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Brightness Frag",
                               &brightness_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load brightness frag shader module", err);
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
                  .module = brightness_vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = brightness_frag_mod,
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
                                         "Brightness Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create brightness pipeline", err);

  tb_rnd_destroy_shader(render_system, brightness_vert_mod);
  tb_rnd_destroy_shader(render_system, brightness_frag_mod);

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

void record_ssao(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                 uint32_t batch_count, const DrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }
  TracyCZoneNC(ctx, "SSAO Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "SSAO", 3, true);
  cmd_begin_label(buffer, "SSAO", (float4){0.4f, 0.8f, 0.0f, 1.0f});

  const DrawBatch *batch = &batches[0];
  const SSAOBatch *ssao_batch = (const SSAOBatch *)batch->user_batch;

  VkPipelineLayout layout = batch->layout;

  // We may not have a valid view set in which case just give up
  if (ssao_batch->view_set != VK_NULL_HANDLE) {
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(
        buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2,
        (VkDescriptorSet[2]){ssao_batch->ssao_set, ssao_batch->view_set}, 0,
        NULL);
    vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(SSAOPushConstants), &ssao_batch->consts);

    // Just drawing a fullscreen triangle that's generated by the vertex shader
    vkCmdDraw(buffer, 3, 1, 0, 0);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_comp_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const DispatchBatch *batches) {
  TracyCZoneNC(ctx, "Compute Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Compute Copy", 3, true);
  cmd_begin_label(buffer, "Compute Copy", (float4){0.4f, 0.0f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DispatchBatch *batch = &batches[batch_idx];
    const FullscreenBatch *fs_batch =
        (const FullscreenBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &fs_batch->set, 0, NULL);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_ssao_blur(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const DispatchBatch *batches) {
  TracyCZoneNC(ctx, "SSAO Blur Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "SSAO Blur", 3, true);
  cmd_begin_label(buffer, "SSAO Blur", (float4){0.4f, 0.0f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DispatchBatch *batch = &batches[batch_idx];
    const BlurBatch *blur_batch = (const BlurBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &blur_batch->set, 0, NULL);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_brightness(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const DrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Brightness Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Brightness", 3, true);
  cmd_begin_label(buffer, "Brightness", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  record_fullscreen(buffer, batches,
                    (const FullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_bloom_blur(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const DispatchBatch *batches) {
  TracyCZoneNC(ctx, "Bloom Blur Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Bloom Blur", 3, true);
  cmd_begin_label(buffer, "Bloom Blur", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DispatchBatch *batch = &batches[batch_idx];
    const BlurBatch *blur_batch = (const BlurBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &blur_batch->set, 0, NULL);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_tonemapping(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const DrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Tonemapping Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Tonemapping", 3, true);
  cmd_begin_label(buffer, "Tonemapping", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  record_fullscreen(buffer, batches,
                    (const FullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void register_pass(RenderPipelineSystem *self, RenderThread *thread,
                   TbRenderPassId id, uint32_t *command_buffers,
                   uint32_t command_buffer_count) {
  RenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, id);
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

    TB_CHECK(pass->transition_count <= TB_MAX_BARRIERS, "Out of range");
    TbRenderTargetId target_id = pass->attachments[0].attachment;
    VkExtent3D target_ext = tb_render_target_get_mip_extent(
        self->render_target_system, pass->attachments[0].mip, target_id);

    PassContext pass_context = (PassContext){
        .id = id,
        .command_buffer_index = command_buffers[id],
        .attachment_count = pass->attach_count,
        .barrier_count = pass->transition_count,
        .width = target_ext.width,
        .height = target_ext.height,
        .render_info = &pass->info[frame_idx],
    };

    for (uint32_t i = 0; i < pass->attach_count; ++i) {
      pass_context.clear_values[i] = pass->attachments[i].clear_value;
    }
#ifdef TRACY_ENABLE
    SDL_strlcpy(pass_context.label, pass->label, TB_RP_LABEL_LEN);
#endif

    // Construct barriers
    for (uint32_t trans_idx = 0; trans_idx < pass->transition_count;
         ++trans_idx) {
      const PassTransition *transition = &pass->transitions[trans_idx];
      ImageTransition *barrier = &pass_context.barriers[trans_idx];
      *barrier = transition->barrier;
      barrier->barrier.image = tb_render_target_get_image(
          self->render_target_system, frame_idx, transition->render_target);
    }

    TB_DYN_ARR_APPEND(state->pass_contexts, pass_context);
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

  TbRenderPassId id = TB_DYN_ARR_SIZE(self->render_passes);
  TB_DYN_ARR_APPEND(self->render_passes, (RenderPass){0});
  RenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, id);

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
  if (pass->attach_count > 0) {
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

  // Populate rendering info if we target any attachments
  if (pass->attach_count > 0) {
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

bool create_render_pipeline_system_internal(
    RenderPipelineSystem *self, Allocator std_alloc, Allocator tmp_alloc,
    RenderSystem *render_system, RenderTargetSystem *render_target_system,
    ViewSystem *view_system) {
  *self = (RenderPipelineSystem){
      .render_system = render_system,
      .render_target_system = render_target_system,
      .view_system = view_system,
      .tmp_alloc = tmp_alloc,
      .std_alloc = std_alloc,
  };

  // Initialize the render pass array
  TB_DYN_ARR_RESET(self->render_passes, self->std_alloc, 8);

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
    const TbRenderTargetId ssao_buffer = render_target_system->ssao_buffer;
    const TbRenderTargetId hdr_color = render_target_system->hdr_color;
    const TbRenderTargetId depth_copy = render_target_system->depth_buffer_copy;
    const TbRenderTargetId color_copy = render_target_system->color_copy;
    const TbRenderTargetId swapchain_target = render_target_system->swapchain;
    const TbRenderTargetId transparent_depth =
        render_target_system->depth_buffer;
    const TbRenderTargetId shadow_map = render_target_system->shadow_map;
    const TbRenderTargetId brightness = render_target_system->brightness;

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
#ifdef TB_USE_INVERSE_DEPTH
                      .clear_value =
                          (VkClearValue){.depthStencil = {.depth = 0.0f}},
#else
                      .clear_value =
                          (VkClearValue){.depthStencil = {.depth = 1.0f}},
#endif
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
    // Create SSAO pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->opaque_depth_normal_pass},
          .transition_count = 3,
          .transitions =
              (PassTransition[3]){
                  {
                      .render_target = self->render_target_system->ssao_buffer,
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
                          self->render_target_system->normal_buffer,
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
                                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
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
              },
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = ssao_buffer,
                  },
              },
          .name = "SSAO Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId, "Failed to create ssao pass",
                      false);
      self->ssao_pass = id;
    }
    // Create SSAO blur compute pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->ssao_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  {
                      .render_target = self->render_target_system->ssao_buffer,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .dst_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask =
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                      .dstAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT |
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                      .oldLayout =
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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
                      .render_target = self->render_target_system->ssao_scratch,
                      .barrier =
                          {
                              .src_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              .dst_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_NONE,
                                      .dstAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT |
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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
          .name = "SSAO Blur Pass",
      };
      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create ssao blur pass", false);
      self->ssao_blur_pass = id;
    }
    // Create env capture pass
    {
      for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
        uint32_t trans_count = 0;
        PassTransition transitions[1] = {0};

        // Do all mip transitions up-front
        if (i == 0) {
          trans_count = 1;
          transitions[0] = (PassTransition){
              .render_target = env_cube,
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
            .dependencies = (TbRenderPassId[1]){self->ssao_blur_pass},
            .transition_count = trans_count,
            .transitions = transitions,
            .attachment_count = 1,
            .attachments =
                (TbAttachmentInfo[1]){
                    {
                        .mip = i,
                        .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .attachment = env_cube,
                    },
                },
            .name = "Env Capture Pass",
        };

        TbRenderPassId id = create_render_pass(self, &create_info);
        TB_CHECK_RETURN(id != InvalidRenderPassId,
                        "Failed to create env capture pass", false);
        self->env_cap_passes[i] = id;
      }
    }
    // Create irradiance convolution pass
    {
      TbRenderPassCreateInfo create_info = {
          .view_mask = 0x0000003F, // 0b00111111
          .dependency_count = 1,
          .dependencies =
              (TbRenderPassId[1]){
                  self->env_cap_passes[PREFILTER_PASS_COUNT - 1]},
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
                                              .levelCount =
                                                  PREFILTER_PASS_COUNT,
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
            .dependencies = (TbRenderPassId[1]){self->irradiance_pass},
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
      // Note: this doesn't actually depend a previous pass,
      // but for now the pass dependencies system only has one starter node,
      // so everything must be a child of that

      const uint32_t trans_count = 1;
      PassTransition transitions[1] = {
          {
              .render_target = self->render_target_system->shadow_map,
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
          },
      };

      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->ssao_blur_pass},
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
                      .attachment = shadow_map,
                  },
              },
          .name = "Shadow Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId, "Failed to create shadow pass",
                      false);
      self->shadow_pass = id;
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
      PassTransition ssao_trans = {
          .render_target = self->render_target_system->ssao_buffer,
          .barrier =
              {
                  .src_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  .barrier =
                      {
                          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                           VK_ACCESS_SHADER_WRITE_BIT,
                          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                          .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,

                          .subresourceRange =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1,
                              },
                      },
              },
      };
      PassTransition shadow_trans = {
          .render_target = self->render_target_system->shadow_map,
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
      const uint32_t transition_count = 6;
      PassTransition transitions[6] = {0};
      transitions[0] = shadow_trans;
      transitions[1] = irr_trans;
      transitions[2] = filter_trans;
      transitions[3] = color_trans;
      transitions[4] = normal_trans;
      transitions[5] = ssao_trans;

      TbRenderPassCreateInfo create_info = {
          .dependency_count = 2,
          .dependencies =
              (TbRenderPassId[2]){self->ssao_blur_pass, self->shadow_pass},
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
    // Create sky pass
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
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
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
      PassTransition transitions[2] = {
          {
              .render_target = self->render_target_system->hdr_color,
              .barrier =
                  {
                      // We know that the hdr color buffer will need to be r/w
                      // on the fragment and compute stages
                      .src_flags =
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .dst_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask =
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                               VK_ACCESS_SHADER_WRITE_BIT,
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
              .render_target = self->render_target_system->brightness,
              .barrier =
                  {
                      .src_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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
                      .attachment = brightness,
                  },
              },
          .name = "Brightness Pass",
      };

      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create brightness downsample pass", false);
      self->brightness_pass = id;
    }
    // Create luminance compute pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->brightness_pass},
          .name = "Luminance Pass",
      };
      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create luminance pass", false);
      self->luminance_pass = id;
    }
    // Create one pass for downsampling
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->luminance_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  // Need to read brightness
                  {
                      .render_target = self->render_target_system->brightness,
                      .barrier =
                          {
                              .src_flags =
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                              .dst_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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
                                      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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
                  // We need the bloom chain to be readable and writable
                  {
                      .render_target =
                          self->render_target_system->bloom_mip_chain,
                      .barrier =
                          {
                              .src_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              .dst_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              .barrier =
                                  {
                                      .sType =
                                          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_NONE,
                                      .dstAccessMask =
                                          VK_ACCESS_SHADER_READ_BIT |
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                      .subresourceRange =
                                          {
                                              .aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .levelCount = TB_BLOOM_MIPS,
                                              .layerCount = 1,
                                          },
                                  },
                          },
                  },
              },
          .name = "Bloom Downsample",
      };
      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create bloom downsample pass", false);
      self->bloom_downsample_pass = id;
    }
    // And one for upsampling
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){self->bloom_downsample_pass},
          .name = "Bloom Upsample",
      };
      TbRenderPassId id = create_render_pass(self, &create_info);
      TB_CHECK_RETURN(id != InvalidRenderPassId,
                      "Failed to create bloom upsample pass", false);
      self->bloom_upsample_pass = id;
    }

    // Create tonemapping pass
    {
      const uint32_t trans_count = 1;
      // Need to read bloom mip chain (mip 0 only)
      PassTransition transitions[1] = {
          {
              .render_target = self->render_target_system->bloom_mip_chain,
              .barrier =
                  {
                      .src_flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      .dst_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      .barrier =
                          {
                              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                               VK_ACCESS_SHADER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                              .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
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
          .dependencies = (TbRenderPassId[1]){self->bloom_upsample_pass},
          .transition_count = trans_count,
          .transitions = transitions,
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
  const uint32_t pass_count = TB_DYN_ARR_SIZE(self->render_passes);
  self->pass_order = tb_alloc_nm_tp(self->std_alloc, pass_count, uint32_t);

  sort_pass_graph(self);

  // Once we've sorted passes, go through the passes
  // in execution order and determine where full pipelines are used.
  // Every time we return to the top of the pipeline, we want to keep track
  // so we can use a different command buffer.
  {
    uint32_t command_buffer_count = 0; // Treated as an index while builiding
    // Worst case each pass needs its own command buffer
    uint32_t *command_buffer_indices =
        tb_alloc_nm_tp(self->tmp_alloc, pass_count, uint32_t);

    {
      // Actually it's just faster if each pass gets their own command list for
      // now
      command_buffer_count = pass_count;
      for (uint32_t pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
        command_buffer_indices[pass_idx] = pass_idx;
      }

      // Ideally we'd want to have a desired # of command lists and space
      // out recording across those but that's harder to actually implement
      // And we can alieviate most of that if we just multithread command
      // list recording

      // Register passes in execution order
      for (uint32_t pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
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

    // Copy
    {
#if 1 // Formatting lmao
      {
        VkSamplerCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .maxLod = 1.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        };
        err = tb_rnd_create_sampler(render_system, &create_info, "Copy Sampler",
                                    &self->sampler);
        TB_VK_CHECK_RET(err, "Failed to create copy sampler", err);
      }
#endif

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
                                       "Copy Descriptor Set Layout",
                                       &self->copy_set_layout);
        TB_VK_CHECK_RET(err, "Failed to create copy descriptor set layout",
                        false);
      }

      {
        VkDescriptorSetLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 3,
            .pBindings = (VkDescriptorSetLayoutBinding[3]){
                {
                    .binding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                {
                    .binding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                {
                    .binding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                    .pImmutableSamplers = &self->sampler,
                },
            }};
        err = tb_rnd_create_set_layout(render_system, &create_info,
                                       "Compute Copy Descriptor Set Layout",
                                       &self->comp_copy_set_layout);
        TB_VK_CHECK_RET(
            err, "Failed to create compute copy descriptor set layout", false);
      }

      {
        VkDescriptorSetLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 4,
            .pBindings = (VkDescriptorSetLayoutBinding[4]){
                {
                    .binding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 3,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &self->sampler,
                },
            }};
        err = tb_rnd_create_set_layout(render_system, &create_info,
                                       "Tonemap Descriptor Set Layout",
                                       &self->tonemap_set_layout);
        TB_VK_CHECK_RET(err, "Failed to create tonemap set layout", false);
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
                                            "Copy Pipeline Layout",
                                            &self->copy_pipe_layout);
        TB_VK_CHECK_RET(err, "Failed to create copy pipeline layout", false);
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts =
                (VkDescriptorSetLayout[1]){
                    self->tonemap_set_layout,
                },
        };
        err = tb_rnd_create_pipeline_layout(self->render_system, &create_info,
                                            "Tonemap Pipeline Layout",
                                            &self->tonemap_pipe_layout);
        TB_VK_CHECK_RET(err, "Failed to create tonemap pipeline layout", false);
      }

      {
        VkDescriptorSetLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 6,
            .pBindings = (VkDescriptorSetLayoutBinding[6]){
                {
                    .binding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &self->sampler,
                },
                {
                    .binding = 3,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 4,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 5,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &self->sampler,
                },
            }};
        err = tb_rnd_create_set_layout(render_system, &create_info,
                                       "SSAO Descriptor Set Layout",
                                       &self->ssao_set_layout);
        TB_VK_CHECK_RET(err, "Failed to create ssao set layout", false);
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 2,
            .pSetLayouts =
                (VkDescriptorSetLayout[2]){
                    self->ssao_set_layout,
                    self->view_system->set_layout,
                },
            .pushConstantRangeCount = 1,
            .pPushConstantRanges =
                (VkPushConstantRange[1]){
                    {
                        .offset = 0,
                        .size = sizeof(SSAOPushConstants),
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                },
        };
        err = tb_rnd_create_pipeline_layout(self->render_system, &create_info,
                                            "SSAO Pipeline Layout",
                                            &self->ssao_pipe_layout);
        TB_VK_CHECK_RET(err, "Failed to create ssao pipeline layout", false);
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts =
                (VkDescriptorSetLayout[1]){self->comp_copy_set_layout},
        };
        err = tb_rnd_create_pipeline_layout(self->render_system, &create_info,
                                            "Comp Copy Pipeline Layout",
                                            &self->comp_copy_pipe_layout);
        TB_VK_CHECK_RET(err, "Failed to create compute copy pipeline layout",
                        false);
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
                                    self->copy_pipe_layout,
                                    &self->depth_copy_pipe);
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
      tb_render_pipeline_get_attachments(self, self->color_copy_pass,
                                         &attach_count, NULL);
      TB_CHECK_RETURN(attach_count == 1, "Unexpected", false);
      PassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(self, self->color_copy_pass,
                                         &attach_count, &attach_info);

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

    // SSAO
    {
      // Create SSAO kernel buffer and noise texture
      {
        // Kernel Buffer
        TbHostBuffer tmp_ssao_params = {0};
        VkResult err = tb_rnd_sys_alloc_tmp_host_buffer(
            self->render_system, sizeof(SSAOParams), 16, &tmp_ssao_params);
        TB_VK_CHECK(err, "Failed to create tmp host buffer for ssao params");

        // Fill out buffer on the CPU side
        {
          SSAOParams params = {
              .kernel_size = SSAO_KERNEL_SIZE,
          };
          for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i) {
            params.kernel[i] = normf3((float3){
                tb_randf(-1.0f, 1.0f),
                tb_randf(-1.0f, 1.0f),
                tb_randf(0.0f, 1.0f),
            });

            // Distribute samples in the hemisphere
            params.kernel[i] *= tb_randf(0.0f, 1.0f);

            float scale = (float)i / SSAO_KERNEL_SIZE;
            scale = lerpf(0.1f, 1.0f, scale * scale);
            params.kernel[i] *= scale;
          }

          SDL_memcpy(tmp_ssao_params.ptr, &params, sizeof(SSAOParams));
        }

        // Create GPU version
        VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .size = sizeof(SSAOParams),
        };
        err = tb_rnd_sys_alloc_gpu_buffer(self->render_system, &create_info,
                                          "SSAO Params", &self->ssao_params);
        TB_VK_CHECK(err, "Failed to create gpu buffer for ssao params");
        // Schedule upload
        BufferCopy upload = {
            .dst = self->ssao_params.buffer,
            .src = tmp_ssao_params.buffer,
            .region = {.size = sizeof(SSAOParams)},
        };
        tb_rnd_upload_buffers(self->render_system, &upload, 1);

        // Noise Texture
        const uint32_t noise_tex_dim = 4;
        const uint32_t noise_tex_size =
            noise_tex_dim * noise_tex_dim * sizeof(float2);
        TbHostBuffer tmp_ssao_noise = {0};
        err = tb_rnd_sys_alloc_tmp_host_buffer(
            self->render_system, noise_tex_size, 16, &tmp_ssao_noise);
        TB_VK_CHECK(err, "Failed to create tmp host buffer for ssao noise");

        // Fill out buffer on the CPU side
        {
          float2 noise[16] = {{0}};
          for (uint32_t i = 0; i < 16; ++i) {
            noise[i] = normf2((float2){
                tb_randf(-1.0f, 1.0f),
                tb_randf(-1.0f, 1.0f),
            });
          }

          SDL_memcpy(tmp_ssao_noise.ptr, noise, noise_tex_size);
        }

        // Create GPU Image
        {
          VkImageCreateInfo create_info = {
              .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
              .imageType = VK_IMAGE_TYPE_2D,
              .format = VK_FORMAT_R8G8_SNORM,

              .extent =
                  (VkExtent3D){
                      .width = noise_tex_dim,
                      .height = noise_tex_dim,
                      .depth = 1,
                  },
              .mipLevels = 1,
              .arrayLayers = 1,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .usage =
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          };
          err = tb_rnd_sys_alloc_gpu_image(self->render_system, &create_info,
                                           "SSAO Noise", &self->ssao_noise);
          TB_VK_CHECK(err, "Failed to create gpu image for ssao params");
        }

        {
          VkImageViewCreateInfo create_info = {
              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
              .image = self->ssao_noise.image,
              .viewType = VK_IMAGE_VIEW_TYPE_2D,
              .format = VK_FORMAT_R8G8_SNORM,
              .subresourceRange =
                  {
                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                      .layerCount = 1,
                      .levelCount = 1,
                  },
          };
          err = tb_rnd_create_image_view(self->render_system, &create_info,
                                         "SSAO Noise View",
                                         &self->ssao_noise_view);
        }

        BufferImageCopy image_copy = {
            .src = tmp_ssao_noise.buffer,
            .dst = self->ssao_noise.image,
            .region =
                {
                    .bufferOffset = tmp_ssao_noise.offset,
                    .imageSubresource =
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .layerCount = 1,
                        },
                    .imageExtent =
                        {
                            .width = noise_tex_dim,
                            .height = noise_tex_dim,
                            .depth = 1,
                        },
                },
            .range =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                    .levelCount = 1,
                },
        };
        tb_rnd_upload_buffer_to_image(self->render_system, &image_copy, 1);
      }

      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(self, self->ssao_pass, &attach_count,
                                         NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      PassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(self, self->ssao_pass, &attach_count,
                                         &attach_info);

      VkFormat color_format = tb_render_target_get_format(
          self->render_target_system, attach_info.attachment);

      err = create_ssao_pipeline(self->render_system, color_format,
                                 self->ssao_pipe_layout, &self->ssao_pipe);
      TB_VK_CHECK_RET(err, "Failed to create ssao pipeline", false);

      DrawContextDescriptor desc = {
          .batch_size = sizeof(SSAOBatch),
          .draw_fn = record_ssao,
          .pass_id = self->ssao_pass,
      };
      self->ssao_ctx = tb_render_pipeline_register_draw_context(self, &desc);
      TB_CHECK_RETURN(self->ssao_ctx != InvalidDrawContextId,
                      "Failed to create ssao draw context", false);
    }

    // Compute Copy
    {
      err = create_comp_copy_pipeline(self->render_system,
                                      self->comp_copy_pipe_layout,
                                      &self->comp_copy_pipe);
      TB_VK_CHECK_RET(err, "Failed to create compute copy pipeline", false);

      // Contexts for specific copy operations
      DispatchContextDescriptor desc = {
          .batch_size = sizeof(FullscreenBatch),
          .dispatch_fn = record_comp_copy,
          .pass_id = self->bloom_blur_pass,
      };
      self->bloom_copy_ctx =
          tb_render_pipeline_register_dispatch_context(self, &desc);
      TB_CHECK_RETURN(self->bloom_copy_ctx != InvalidDispatchContextId,
                      "Failed to create compute copy dispatch context", false);
    }

    // Compute Blur
    {
      {
        VkDescriptorSetLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 3,
            .pBindings = (VkDescriptorSetLayoutBinding[3]){
                {
                    .binding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                {
                    .binding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                {
                    .binding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                    .pImmutableSamplers = &self->sampler,
                },
            }};
        err =
            tb_rnd_create_set_layout(render_system, &create_info,
                                     "Blur Set Layout", &self->blur_set_layout);
        TB_VK_CHECK_RET(err, "Failed to create blur descriptor set layout",
                        false);
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts =
                (VkDescriptorSetLayout[1]){
                    self->blur_set_layout,
                },
        };
        err = tb_rnd_create_pipeline_layout(self->render_system, &create_info,
                                            "Blur Pipeline Layout",
                                            &self->blur_pipe_layout);
        TB_VK_CHECK_RET(err, "Failed to create blur pipeline layout", false);
      }

      err = create_blur_pipelines(self->render_system, self->blur_pipe_layout,
                                  &self->blur_h_pipe, &self->blur_v_pipe);
      TB_VK_CHECK_RET(err, "Failed to create blur pipeline", false);

      DispatchContextDescriptor desc = {
          .batch_size = sizeof(BlurBatch),
          .dispatch_fn = record_ssao_blur,
          .pass_id = self->ssao_blur_pass,
      };
      self->ssao_blur_ctx =
          tb_render_pipeline_register_dispatch_context(self, &desc);
      TB_CHECK_RETURN(self->ssao_blur_ctx != InvalidDispatchContextId,
                      "Failed to create ssao blur dispatch context", false);
    }

    // Create bloom work
    err = create_downsample_work(self->render_system, self, self->sampler,
                                 self->bloom_downsample_pass,
                                 &self->downsample_work);
    err = create_upsample_work(self->render_system, self, self->sampler,
                               self->bloom_upsample_pass, &self->upsample_work);

    // Compute Luminance Histogram and Average work
    err = create_lum_hist_work(self->render_system, self, self->sampler,
                               self->luminance_pass, &self->lum_hist_work);
    err = create_lum_avg_work(self->render_system, self, self->luminance_pass,
                              &self->lum_avg_work);

    // Brightness
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(self, self->brightness_pass,
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      PassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(self, self->brightness_pass,
                                         &attach_count, &attach_info);

      VkFormat color_format = tb_render_target_get_format(
          self->render_target_system, attach_info.attachment);

      err = create_brightness_pipeline(self->render_system, color_format,
                                       self->copy_pipe_layout,
                                       &self->brightness_pipe);
      TB_VK_CHECK_RET(err, "Failed to create brightness pipeline", false);

      DrawContextDescriptor desc = {
          .batch_size = sizeof(FullscreenBatch),
          .draw_fn = record_brightness,
          .pass_id = self->brightness_pass,
      };
      self->brightness_ctx =
          tb_render_pipeline_register_draw_context(self, &desc);
      TB_CHECK_RETURN(self->brightness_ctx != InvalidDrawContextId,
                      "Failed to create brightness draw context", false);
    }

    // Blur
    {
      DispatchContextDescriptor desc = {
          .batch_size = sizeof(BlurBatch),
          .dispatch_fn = record_bloom_blur,
          .pass_id = self->bloom_blur_pass,
      };
      self->bloom_blur_ctx =
          tb_render_pipeline_register_dispatch_context(self, &desc);
      TB_CHECK_RETURN(self->bloom_blur_ctx != InvalidDispatchContextId,
                      "Failed to create bloom blur dispatch context", false);
    }

    // Tonemapping
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(self, self->tonemap_pass,
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      PassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(self, self->tonemap_pass,
                                         &attach_count, &attach_info);

      VkFormat swap_target_format = tb_render_target_get_format(
          self->render_target_system, attach_info.attachment);

      err = create_tonemapping_pipeline(self->render_system, swap_target_format,
                                        self->tonemap_pipe_layout,
                                        &self->tonemap_pipe);
      TB_VK_CHECK_RET(err, "Failed to create tonemapping pipeline", false);

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

  return true;
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
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find view system which the render "
                  "pipeline depends on",
                  false);
  return create_render_pipeline_system_internal(
      self, desc->std_alloc, desc->tmp_alloc, render_system,
      render_target_system, view_system);
}

void destroy_render_pipeline_system(RenderPipelineSystem *self) {
  tb_rnd_destroy_sampler(self->render_system, self->sampler);
  tb_rnd_destroy_set_layout(self->render_system, self->ssao_set_layout);
  tb_rnd_destroy_set_layout(self->render_system, self->blur_set_layout);
  tb_rnd_destroy_set_layout(self->render_system, self->copy_set_layout);
  tb_rnd_destroy_set_layout(self->render_system, self->comp_copy_set_layout);
  tb_rnd_destroy_set_layout(self->render_system, self->tonemap_set_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->ssao_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->blur_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->copy_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->comp_copy_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->tonemap_pipe_layout);
  tb_rnd_destroy_pipeline(self->render_system, self->ssao_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->blur_h_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->blur_v_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->depth_copy_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->color_copy_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->comp_copy_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->brightness_pipe);
  tb_rnd_destroy_pipeline(self->render_system, self->tonemap_pipe);

  destroy_downsample_work(self->render_system, &self->downsample_work);
  destroy_upsample_work(self->render_system, &self->upsample_work);

  destroy_lum_avg_work(self->render_system, &self->lum_avg_work);
  destroy_lum_hist_work(self->render_system, &self->lum_hist_work);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->render_system,
                                   self->descriptor_pools[i].set_pool);
  }

  tb_rnd_free_gpu_buffer(self->render_system, &self->ssao_params);
  tb_rnd_free_gpu_image(self->render_system, &self->ssao_noise);
  tb_rnd_destroy_image_view(self->render_system, self->ssao_noise_view);

  TB_DYN_ARR_DESTROY(self->render_passes);
  tb_free(self->std_alloc, self->pass_order);

  *self = (RenderPipelineSystem){0};
}

void reimport_render_pass(RenderPipelineSystem *self, TbRenderPassId id) {
  RenderPass *rp = &TB_DYN_ARR_AT(self->render_passes, id);

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
        PassContext *context = &TB_DYN_ARR_AT(state->pass_contexts, id);
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
    reimport_render_pass(self, self->ssao_pass);
    reimport_render_pass(self, self->opaque_color_pass);
    reimport_render_pass(self, self->depth_copy_pass);
    reimport_render_pass(self, self->color_copy_pass);
    reimport_render_pass(self, self->sky_pass);
    reimport_render_pass(self, self->transparent_depth_pass);
    reimport_render_pass(self, self->transparent_color_pass);
    reimport_render_pass(self, self->brightness_pass);
    reimport_render_pass(self, self->bloom_blur_pass);
    reimport_render_pass(self, self->tonemap_pass);
    reimport_render_pass(self, self->ui_pass);
  }

  // We now need to patch every pass's transitions so that their targets point
  // at the right VkImages
  TB_DYN_ARR_FOREACH(self->render_passes, pass_idx) {
    RenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, pass_idx);
    for (uint32_t trans_idx = 0; trans_idx < pass->transition_count;
         ++trans_idx) {
      for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES;
           ++frame_idx) {
        FrameState *state =
            &self->render_system->render_thread->frame_states[frame_idx];
        PassContext *context = &TB_DYN_ARR_AT(state->pass_contexts, pass_idx);
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
    TB_DYN_ARR_FOREACH(state->draw_contexts, ctx_idx) {
      DrawContext *draw_ctx = &TB_DYN_ARR_AT(state->draw_contexts, ctx_idx);
      draw_ctx->batch_count = 0;
    }
    TB_DYN_ARR_FOREACH(state->dispatch_contexts, ctx_idx) {
      DispatchContext *dispatch_ctx =
          &TB_DYN_ARR_AT(state->dispatch_contexts, ctx_idx);
      dispatch_ctx->batch_count = 0;
    }
  }
}

void tick_render_pipeline_system_internal(RenderPipelineSystem *self,
                                          const SystemInput *input,
                                          SystemOutput *output,
                                          float delta_seconds) {
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "Render Pipeline System Tick", TracyCategoryColorRendering,
               true);

  // A few passes will be driven from here because an external system
  // has no need to directly drive these passes

  // Allocate and write all core descriptor sets
  {
    VkResult err = VK_SUCCESS;
    // Allocate the known descriptor sets we need for this frame
    {
#define SET_COUNT 14
      VkDescriptorPoolCreateInfo pool_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = SET_COUNT * 4,
          .poolSizeCount = 3,
          .pPoolSizes =
              (VkDescriptorPoolSize[3]){
                  {
                      .descriptorCount = SET_COUNT * 4,
                      .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  },
                  {
                      .descriptorCount = SET_COUNT * 4,
                      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                  },
                  {
                      .descriptorCount = SET_COUNT * 4,
                      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                  },
              },
      };
      VkDescriptorSetLayout layouts[SET_COUNT] = {
          self->ssao_set_layout,
          self->blur_set_layout,
          self->blur_set_layout,
          self->copy_set_layout,
          self->copy_set_layout,
          self->lum_hist_work.set_layout,
          self->lum_avg_work.set_layout,
          self->downsample_work.set_layout,
          self->downsample_work.set_layout,
          self->downsample_work.set_layout,
          self->upsample_work.set_layout,
          self->upsample_work.set_layout,
          self->upsample_work.set_layout,
          self->tonemap_set_layout,
      };
      err =
          tb_rnd_frame_desc_pool_tick(self->render_system, &pool_info, layouts,
                                      self->descriptor_pools, SET_COUNT);
      TB_VK_CHECK(err, "Failed to tick descriptor pool");
#undef SET_COUNT
    }

    VkDescriptorSet ssao_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 0);
    VkDescriptorSet ssao_x_blur_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 1);
    VkDescriptorSet ssao_y_blur_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 2);
    VkDescriptorSet depth_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 3);
    VkDescriptorSet color_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 4);
    VkDescriptorSet lum_hist_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 5);
    VkDescriptorSet lum_avg_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 6);
    VkDescriptorSet down_half_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 7);
    VkDescriptorSet down_quarter_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 8);
    VkDescriptorSet down_sixteen_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 9);
    VkDescriptorSet up_quarter_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 10);
    VkDescriptorSet up_half_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 11);
    VkDescriptorSet up_full_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 12);
    VkDescriptorSet tonemap_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 13);

    VkImageView ssao_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->ssao_buffer);
    VkImageView ssao_scratch_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->ssao_scratch);
    VkImageView depth_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->depth_buffer);
    VkImageView normal_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->normal_buffer);
    VkImageView color_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->hdr_color);
    VkBuffer lum_hist_buffer = self->lum_hist_work.lum_histogram.buffer;
    VkBuffer lum_avg_buffer = self->lum_avg_work.lum_avg.buffer;
    VkImageView brightness_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->brightness);
    VkImageView bloom_full_view = tb_render_target_get_mip_view(
        self->render_target_system, 0, self->render_system->frame_idx,
        self->render_target_system->bloom_mip_chain);
    VkImageView bloom_half_view = tb_render_target_get_mip_view(
        self->render_target_system, 1, self->render_system->frame_idx,
        self->render_target_system->bloom_mip_chain);
    VkImageView bloom_quarter_view = tb_render_target_get_mip_view(
        self->render_target_system, 2, self->render_system->frame_idx,
        self->render_target_system->bloom_mip_chain);
    VkImageView bloom_sixteenth_view = tb_render_target_get_mip_view(
        self->render_target_system, 3, self->render_system->frame_idx,
        self->render_target_system->bloom_mip_chain);

// Write the descriptor set
#define WRITE_COUNT 29
    VkWriteDescriptorSet writes[WRITE_COUNT] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_set,
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
            .dstSet = ssao_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .imageView = normal_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_set,
            .dstBinding = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .offset = self->ssao_params.info.offset,
                    .range = sizeof(SSAOParams),
                    .buffer = self->ssao_params.buffer,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_set,
            .dstBinding = 4,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .imageView = self->ssao_noise_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_x_blur_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = ssao_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_x_blur_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = ssao_scratch_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_y_blur_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = ssao_scratch_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ssao_y_blur_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = ssao_view,
                },
        },
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
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = lum_hist_set,
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
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = lum_hist_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = lum_hist_buffer,
                    .range = sizeof(uint32_t) * 256, // Hack :(
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = lum_avg_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = lum_hist_buffer,
                    .range = sizeof(uint32_t) * 256, // Hack :(
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = lum_avg_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = lum_avg_buffer,
                    .range = sizeof(float), // Hack :(
                },
        },
        // Downsample writes
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = down_half_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = brightness_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = down_half_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_half_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = down_quarter_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_half_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = down_quarter_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_quarter_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = down_sixteen_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_quarter_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = down_sixteen_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_sixteenth_view,
                },
        },
        // Upsample writes
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = up_quarter_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_sixteenth_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = up_quarter_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_quarter_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = up_half_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_quarter_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = up_half_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_half_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = up_full_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_half_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = up_full_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .imageView = bloom_full_view,
                },
        },
        // Tonemap writes
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = tonemap_set,
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
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = tonemap_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo =
                &(VkDescriptorImageInfo){
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .imageView = bloom_full_view,
                },
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = tonemap_set,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = lum_avg_buffer,
                    .range = sizeof(float), // Hack :(
                },
        },
    };
    vkUpdateDescriptorSets(self->render_system->render_thread->device,
                           WRITE_COUNT, writes, 0, NULL);
#undef WRITE_COUNT
  }

  // Issue draws for full screen passes
  {
    VkDescriptorSet ssao_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 0);
    VkDescriptorSet ssao_blur_x_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 1);
    VkDescriptorSet ssao_blur_y_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 2);
    VkDescriptorSet depth_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 3);
    VkDescriptorSet color_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 4);
    VkDescriptorSet lum_hist_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 5);
    VkDescriptorSet lum_avg_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 6);
    VkDescriptorSet down_half_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 7);
    VkDescriptorSet down_quarter_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 8);
    VkDescriptorSet down_sixteen_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 9);
    VkDescriptorSet up_quarter_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 10);
    VkDescriptorSet up_half_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 11);
    VkDescriptorSet up_full_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 12);
    VkDescriptorSet tonemap_set = tb_rnd_frame_desc_pool_get_set(
        self->render_system, self->descriptor_pools, 13);

    // TODO: Make this less hacky
    const uint32_t width = self->render_system->render_thread->swapchain.width;
    const uint32_t height =
        self->render_system->render_thread->swapchain.height;

    // SSAO pass
    {
      // HACK - find which view targets the swapchain
      const View *view = NULL;
      TbViewId view_id = InvalidViewId;
      TB_DYN_ARR_FOREACH(self->view_system->views, view_idx) {
        const View *v = &TB_DYN_ARR_AT(self->view_system->views, view_idx);
        if (v->target == self->render_target_system->swapchain) {
          view = v;
          view_id = view_idx;
          break;
        }
      }

      // If we don't have a view, just bail and don't schedule a draw
      if (view) {
        VkDescriptorSet view_set =
            tb_view_system_get_descriptor(self->view_system, view_id);

        SSAOBatch ssao_batch = {
            .ssao_set = ssao_set,
            .view_set = view_set,
            .consts =
                {
                    .noise_scale =
                        (float2){(float)width / 4.0f, (float)height / 4.0f},
                    .radius = 0.5,
                },
        };
        DrawBatch batch = {
            .layout = self->ssao_pipe_layout,
            .pipeline = self->ssao_pipe,
            .viewport = {0, 0, width, height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .user_batch = &ssao_batch,
        };
        tb_render_pipeline_issue_draw_batch(self, self->ssao_ctx, 1, &batch);
      }
    }
    // SSAO Blur Pass
    {
      // Do a blur on each axis
      BlurBatch x_blur_batch = {
          .set = ssao_blur_x_set,
      };
      DispatchBatch x_batch = {
          .layout = self->blur_pipe_layout,
          .pipeline = self->blur_h_pipe,
          .user_batch = &x_blur_batch,
          .group_count = 1,
          .groups[0] = {(width + 64 - 1) / 64, height, 1},

      };
      BlurBatch y_blur_batch = {
          .set = ssao_blur_y_set,
      };
      DispatchBatch y_batch = {
          .layout = self->blur_pipe_layout,
          .pipeline = self->blur_v_pipe,
          .user_batch = &y_blur_batch,
          .group_count = 1,
          .groups[0] = {width, (height + 64 - 1) / 64, 1},
      };

      tb_render_pipeline_issue_dispatch_batch(self, self->ssao_blur_ctx, 1,
                                              &x_batch);
      tb_render_pipeline_issue_dispatch_batch(self, self->ssao_blur_ctx, 1,
                                              &y_batch);
    }
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
    {
      // Configurables
      float min_log_lum = -5.0f;
      float max_log_lum = 10.0f;
      // Luminance histogram gather pass
      {
        uint32_t group_x = (uint32_t)SDL_ceilf((float)width / 16.0f);
        uint32_t group_y = (uint32_t)SDL_ceilf((float)height / 16.0f);
        LuminanceBatch lum_batch = {
            .set = lum_hist_set,
            .consts = {.params = {min_log_lum, 1 / (max_log_lum - min_log_lum),
                                  (float)width, (float)height}},
        };
        DispatchBatch batch = {
            .layout = self->lum_hist_work.pipe_layout,
            .pipeline = self->lum_hist_work.pipeline,
            .user_batch = &lum_batch,
            .group_count = 1,
            .groups[0] = {group_x, group_y, 1},
        };
        tb_render_pipeline_issue_dispatch_batch(self, self->lum_hist_work.ctx,
                                                1, &batch);
      }
      // Luminance average pass
      {
        float time = clampf(1.f - SDL_expf(-delta_seconds * 1.1f), 0, 1);
        LuminanceBatch lum_batch = {
            .set = lum_avg_set,
            .consts = {.params = {min_log_lum, (max_log_lum - min_log_lum),
                                  time, (float)width * (float)height}},
        };
        DispatchBatch batch = {
            .layout = self->lum_avg_work.pipe_layout,
            .pipeline = self->lum_avg_work.pipeline,
            .user_batch = &lum_batch,
            .group_count = 1,
            .groups[0] = {1, 1, 1},
        };
        tb_render_pipeline_issue_dispatch_batch(self, self->lum_avg_work.ctx, 1,
                                                &batch);
      }
    }
    // Brightness pass
    {
      FullscreenBatch fs_batch = {
          .set = color_set,
      };
      DrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline = self->brightness_pipe,
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->brightness_ctx, 1,
                                          &batch);
    }
    // Blur passes
    {
#define BLUR_BATCH_COUNT TB_BLOOM_MIPS - 1
      DownsampleBatch downsample_batches[BLUR_BATCH_COUNT] = {
          {.set = down_half_set},
          {.set = down_quarter_set},
          {.set = down_sixteen_set},
      };
      DispatchBatch down_batches[BLUR_BATCH_COUNT] = {
          {
              .layout = self->downsample_work.pipe_layout,
              .pipeline = self->downsample_work.pipeline,
              .user_batch = &downsample_batches[0],
              .group_count = 1,
              .groups[0] = {(width / 16) + 1, (height / 16) + 1, 1},
          },
          {
              .layout = self->downsample_work.pipe_layout,
              .pipeline = self->downsample_work.pipeline,
              .user_batch = &downsample_batches[1],
              .group_count = 1,
              .groups[0] = {((width / 2) / 16) + 1, ((height / 2) / 16) + 1, 1},
          },
          {
              .layout = self->downsample_work.pipe_layout,
              .pipeline = self->downsample_work.pipeline,
              .user_batch = &downsample_batches[2],
              .group_count = 1,
              .groups[0] = {((width / 4) / 16) + 1, ((height / 4) / 16) + 1, 1},
          },
      };
      tb_render_pipeline_issue_dispatch_batch(self, self->downsample_work.ctx,
                                              BLUR_BATCH_COUNT, down_batches);

      UpsampleBatch upsample_batches[BLUR_BATCH_COUNT] = {
          {.set = up_quarter_set},
          {.set = up_half_set},
          {.set = up_full_set},
      };
      DispatchBatch up_batches[BLUR_BATCH_COUNT] = {
          {
              .layout = self->upsample_work.pipe_layout,
              .pipeline = self->upsample_work.pipeline,
              .user_batch = &upsample_batches[0],
              .group_count = 1,
              .groups[0] = {((width / 4) / 16) + 1, ((height / 4) / 16) + 1, 1},
          },
          {
              .layout = self->upsample_work.pipe_layout,
              .pipeline = self->upsample_work.pipeline,
              .user_batch = &upsample_batches[1],
              .group_count = 1,
              .groups[0] = {((width / 2) / 16) + 1, ((height / 2) / 16) + 1, 1},
          },
          {
              .layout = self->upsample_work.pipe_layout,
              .pipeline = self->upsample_work.pipeline,
              .user_batch = &upsample_batches[2],
              .group_count = 1,
              .groups[0] = {(width / 16) + 1, (height / 16) + 1, 1},
          },
      };
      tb_render_pipeline_issue_dispatch_batch(self, self->upsample_work.ctx,
                                              BLUR_BATCH_COUNT, up_batches);
#undef BLUR_BATCH_COUNT
    }

    // Tonemapping pass
    {
      FullscreenBatch fs_batch = {
          .set = tonemap_set,
      };
      DrawBatch batch = {
          .layout = self->tonemap_pipe_layout,
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

void tick_render_pipeline_system(void *self, const SystemInput *input,
                                 SystemOutput *output, float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick RenderPipeline System");
  tick_render_pipeline_system_internal((RenderPipelineSystem *)self, input,
                                       output, delta_seconds);
}

void check_swapchain_resize(void *self, const SystemInput *input,
                            SystemOutput *output, float delta_seconds) {
  (void)input;
  (void)output;
  (void)delta_seconds;
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick RenderPipeline Check Resize");
  RenderPipelineSystem *rnd_pipe_sys = (RenderPipelineSystem *)self;
  RenderSystem *rnd_sys = rnd_pipe_sys->render_system;
  if (rnd_sys->render_thread->swapchain_resize_signal) {
    TracyCZoneN(resize_ctx, "Resize", true);
    tb_rnd_on_swapchain_resize(rnd_pipe_sys);

    rnd_sys->frame_idx = 0;

    // Re-create all render thread semaphores
    for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
      SDL_DestroySemaphore(
          rnd_sys->render_thread->frame_states[frame_idx].wait_sem);
      rnd_sys->render_thread->frame_states[frame_idx].wait_sem =
          SDL_CreateSemaphore(1);
    }

    // Let the render thread know we're done handling the resize on the
    // main thread
    SDL_SemPost(rnd_sys->render_thread->resized);

    // Let the render thread process frame index 0
    tb_wait_render(rnd_sys->render_thread, rnd_sys->frame_idx);
    TracyCZoneEnd(resize_ctx);
  }
}

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc) {
  *desc = (SystemDescriptor){
      .name = "Render Pipeline",
      .size = sizeof(RenderPipelineSystem),
      .id = RenderPipelineSystemId,
      .desc = (InternalDescriptor)pipe_desc,
      .system_dep_count = 3,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = RenderTargetSystemId,
      .system_deps[2] = ViewSystemId,
      .create = tb_create_render_pipeline_system,
      .destroy = tb_destroy_render_pipeline_system,
      .tick_fn_count = 2,
      .tick_fns[0] =
          {
              .system_id = RenderPipelineSystemId,
              .order =
                  E_TICK_TOP_OF_FRAME + 1, // We need this tick to happen right
                                           // after the render system ticks
              .function = check_swapchain_resize,
          },
      .tick_fns[1] =
          {
              .system_id = RenderPipelineSystemId,
              .order = E_TICK_RENDER - 1, // Tick before the render system
              .function = tick_render_pipeline_system,
          },
  };
}

TbDrawContextId
tb_render_pipeline_register_draw_context(RenderPipelineSystem *self,
                                         const DrawContextDescriptor *desc) {
  RenderThread *thread = self->render_system->render_thread;
  TbDrawContextId id = TB_DYN_ARR_SIZE(thread->frame_states[0].draw_contexts);
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state = &thread->frame_states[frame_idx];
    DrawContext ctx = {
        .pass_id = desc->pass_id,
        .user_batch_size = desc->batch_size,
        .record_fn = desc->draw_fn,
    };
    TB_DYN_ARR_APPEND(state->draw_contexts, ctx);
  }
  return id;
}

TbDispatchContextId tb_render_pipeline_register_dispatch_context(
    RenderPipelineSystem *self, const DispatchContextDescriptor *desc) {
  RenderThread *thread = self->render_system->render_thread;
  TbDispatchContextId id =
      TB_DYN_ARR_SIZE(thread->frame_states[0].dispatch_contexts);
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state = &thread->frame_states[frame_idx];
    DispatchContext ctx = {
        .pass_id = desc->pass_id,
        .user_batch_size = desc->batch_size,
        .record_fn = desc->dispatch_fn,
    };
    TB_DYN_ARR_APPEND(state->dispatch_contexts, ctx);
  }
  return id;
}

void tb_render_pipeline_get_attachments(RenderPipelineSystem *self,
                                        TbRenderPassId pass,
                                        uint32_t *attach_count,
                                        PassAttachment *attachments) {
  TB_CHECK(pass < TB_DYN_ARR_SIZE(self->render_passes), "Pass Id out of range");
  TB_CHECK(attach_count, "Attachment count pointer must be valid");
  TB_CHECK(*attach_count <= TB_MAX_ATTACHMENTS, "Too many attachments");

  const RenderPass *p = &TB_DYN_ARR_AT(self->render_passes, pass);

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
  if (draw_ctx >= TB_DYN_ARR_SIZE(state->draw_contexts)) {
    TB_CHECK(false, "Draw Context Id out of range");
    return;
  }

  DrawContext *ctx = &TB_DYN_ARR_AT(state->draw_contexts, draw_ctx);

  const uint32_t write_head = ctx->batch_count;
  const uint32_t new_count = ctx->batch_count + batch_count;
  if (new_count > ctx->batch_max) {
    const uint32_t new_max = new_count * 2;
    // We want to realloc the user batches first because their pointers
    // changing is what we have to fix up
    ctx->user_batches = tb_realloc(self->std_alloc, ctx->user_batches,
                                   new_max * ctx->user_batch_size);
    ctx->batches =
        tb_realloc_nm_tp(self->std_alloc, ctx->batches, new_max, DrawBatch);
    // Pointer Fixup
    for (uint32_t i = 0; i < batch_count; ++i) {
      ctx->batches[i].user_batch =
          (uint8_t *)ctx->user_batches + (ctx->user_batch_size * i);
    }

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

void tb_render_pipeline_issue_dispatch_batch(RenderPipelineSystem *self,
                                             TbDispatchContextId dispatch_ctx,
                                             uint32_t batch_count,
                                             const DispatchBatch *batches) {
  RenderThread *thread = self->render_system->render_thread;
  FrameState *state = &thread->frame_states[self->render_system->frame_idx];
  if (dispatch_ctx >= TB_DYN_ARR_SIZE(state->dispatch_contexts)) {
    TB_CHECK(false, "Dispatch Context Id out of range");
    return;
  }

  DispatchContext *ctx = &TB_DYN_ARR_AT(state->dispatch_contexts, dispatch_ctx);

  const uint32_t write_head = ctx->batch_count;
  const uint32_t new_count = ctx->batch_count + batch_count;
  if (new_count > ctx->batch_max) {
    const uint32_t new_max = new_count * 2;
    // We want to realloc the user batches first because their pointers
    // changing is what we have to fix up
    ctx->user_batches = tb_realloc(self->std_alloc, ctx->user_batches,
                                   new_max * ctx->user_batch_size);
    ctx->batches =
        tb_realloc_nm_tp(self->std_alloc, ctx->batches, new_max, DispatchBatch);
    // Pointer Fixup
    for (uint32_t i = 0; i < batch_count; ++i) {
      ctx->batches[i].user_batch =
          (uint8_t *)ctx->user_batches + (ctx->user_batch_size * i);
    }

    ctx->batch_max = new_max;
  }

  // Copy batches into frame state's batch list
  DispatchBatch *dst = &ctx->batches[write_head];
  SDL_memcpy(dst, batches, batch_count * sizeof(DispatchBatch));

  for (uint32_t i = 0; i < 0 + batch_count; ++i) {
    void *user_dst = ((uint8_t *)ctx->user_batches) +
                     ((i + write_head) * ctx->user_batch_size);
    SDL_memcpy(user_dst, batches[i].user_batch, ctx->user_batch_size);
    ctx->batches[i + write_head].user_batch = user_dst;
  }

  ctx->batch_count = new_count;
}

void tick_render_pipeline_sys(ecs_iter_t *it) {
  RenderPipelineSystem *sys = ecs_field(it, RenderPipelineSystem, 1);
  tick_render_pipeline_system_internal(sys, NULL, NULL, 0.0f);
}

void tb_register_render_pipeline_sys(ecs_world_t *ecs, Allocator std_alloc,
                                     Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderTargetSystem);
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderTargetSystem *rt_sys = ecs_singleton_get_mut(ecs, RenderTargetSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);

  RenderPipelineSystem sys = {0};
  create_render_pipeline_system_internal(&sys, std_alloc, tmp_alloc, rnd_sys,
                                         rt_sys, view_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(RenderPipelineSystem), RenderPipelineSystem, &sys);

  ECS_SYSTEM(ecs, tick_render_pipeline_sys, EcsPostUpdate,
             RenderPipelineSystem(RenderPipelineSystem))
}

void tb_unregister_render_pipeline_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  RenderPipelineSystem *sys = ecs_singleton_get_mut(ecs, RenderPipelineSystem);
  destroy_render_pipeline_system(sys);
}
