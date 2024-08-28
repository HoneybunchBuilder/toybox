#include "tb_render_pipeline_system.h"

#include "tb_common.h"
#include "tb_profiling.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_shader_common.h"
#include "tb_shader_system.h"
#include "tb_texture_system.h"
#include "tb_view_system.h"
#include "tb_world.h"

#include <flecs.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "brightness_frag.h"
#include "brightness_vert.h"
#include "colorcopy_frag.h"
#include "colorcopy_vert.h"
#include "copy_comp.h"
#include "depthcopy_frag.h"
#include "depthcopy_vert.h"
#include "tonemap_frag.h"
#include "tonemap_vert.h"
#pragma clang diagnostic pop

#define BLUR_BATCH_COUNT (TB_BLOOM_MIPS - 1)

ECS_COMPONENT_DECLARE(TbRenderPipelineSystem);

void tb_register_render_pipeline_sys(TbWorld *world);
void tb_unregister_render_pipeline_sys(TbWorld *world);

TB_REGISTER_SYS(tb, render_pipeline, TB_RP_SYS_PRIO)

typedef struct PassTransition {
  TbRenderTargetId render_target;
  TbImageTransition barrier;
} PassTransition;

typedef struct TbRenderPass {
  uint32_t dep_count;
  TbRenderPassId deps[TB_MAX_RENDER_PASS_DEPS];

  uint32_t transition_count;
  PassTransition transitions[TB_MAX_RENDER_PASS_TRANS];

  uint32_t attach_count;
  TbPassAttachment attachments[TB_MAX_ATTACHMENTS];

  VkRenderingInfo info[TB_MAX_FRAME_STATES];

#ifdef TRACY_ENABLE
  char label[TB_RP_LABEL_LEN];
#endif
} TbRenderPass;

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

void sort_pass_graph(TbRenderPipelineSystem *self) {
  TracyCZoneN(ctx, "Sort Pass Graph", true);
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
      const TbRenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, i);
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

  TracyCZoneEnd(ctx);
}

typedef struct TbPipeShaderArgs {
  TbRenderSystem *rnd_sys;
  VkFormat format;
  VkPipelineLayout pipe_layout;
} TbPipeShaderArgs;

VkPipeline create_depth_pipeline(void *args) {
  tb_auto pipe_args = (const TbPipeShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  VkShaderModule depth_vert_mod = VK_NULL_HANDLE;
  VkShaderModule depth_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(depthcopy_vert);
    create_info.pCode = (const uint32_t *)depthcopy_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Depth Copy Vert",
                         &depth_vert_mod);

    create_info.codeSize = sizeof(depthcopy_frag);
    create_info.pCode = (const uint32_t *)depthcopy_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Depth Copy Frag",
                         &depth_frag_mod);
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Depth Copy Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, depth_vert_mod);
  tb_rnd_destroy_shader(rnd_sys, depth_frag_mod);

  return pipeline;
}

VkPipeline create_color_copy_pipeline(void *args) {
  tb_auto pipe_args = (const TbPipeShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto color_format = pipe_args->format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  VkShaderModule color_vert_mod = VK_NULL_HANDLE;
  VkShaderModule color_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(colorcopy_vert);
    create_info.pCode = (const uint32_t *)colorcopy_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Color Copy Vert",
                         &color_vert_mod);

    create_info.codeSize = sizeof(colorcopy_frag);
    create_info.pCode = (const uint32_t *)colorcopy_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Color Copy Frag",
                         &color_frag_mod);
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Color Copy Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, color_vert_mod);
  tb_rnd_destroy_shader(rnd_sys, color_frag_mod);

  return pipeline;
}

VkPipeline create_comp_copy_pipeline(void *args) {
  tb_auto pipe_args = (const TbPipeShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto layout = pipe_args->pipe_layout;

  VkShaderModule copy_comp_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(copy_comp);
    create_info.pCode = (const uint32_t *)copy_comp;
    tb_rnd_create_shader(rnd_sys, &create_info, "Copy Comp", &copy_comp_mod);
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_compute_pipelines(rnd_sys, 1, &create_info,
                                  "Compute Copy Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, copy_comp_mod);

  return pipeline;
}

VkPipeline create_brightness_pipeline(void *args) {
  tb_auto pipe_args = (const TbPipeShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto color_format = pipe_args->format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  VkShaderModule brightness_vert_mod = VK_NULL_HANDLE;
  VkShaderModule brightness_frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(brightness_vert);
    create_info.pCode = (const uint32_t *)brightness_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Brightness Vert",
                         &brightness_vert_mod);

    create_info.codeSize = sizeof(brightness_frag);
    create_info.pCode = (const uint32_t *)brightness_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Brightness Frag",
                         &brightness_frag_mod);
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Brightness Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, brightness_vert_mod);
  tb_rnd_destroy_shader(rnd_sys, brightness_frag_mod);

  return pipeline;
}

VkPipeline create_tonemapping_pipeline(void *args) {
  tb_auto pipe_args = (const TbPipeShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto swap_target_format = pipe_args->format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  VkShaderModule tonemap_vert_mod = VK_NULL_HANDLE;
  VkShaderModule tonemap_frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(tonemap_vert);
    create_info.pCode = (const uint32_t *)tonemap_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Tonemapping Vert",
                         &tonemap_vert_mod);

    create_info.codeSize = sizeof(tonemap_frag);
    create_info.pCode = (const uint32_t *)tonemap_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Tonemapping Frag",
                         &tonemap_frag_mod);
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
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = tonemap_frag_mod,
                  .pName = "main",
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Tonmapping Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, tonemap_vert_mod);
  tb_rnd_destroy_shader(rnd_sys, tonemap_frag_mod);

  return pipeline;
}

void record_depth_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Depth Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Depth Copy", 3, true);
  cmd_begin_label(buffer, "Depth Copy", (float4){0.8f, 0.0f, 0.4f, 1.0f});

  tb_record_fullscreen(buffer, batches,
                       (const TbFullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_color_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Color Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Color Copy", 3, true);
  cmd_begin_label(buffer, "Color Copy", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  tb_record_fullscreen(buffer, batches,
                       (const TbFullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_comp_copy(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const TbDispatchBatch *batches) {
  TracyCZoneNC(ctx, "Compute Copy Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Compute Copy", 3, true);
  cmd_begin_label(buffer, "Compute Copy", (float4){0.4f, 0.0f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDispatchBatch *batch = &batches[batch_idx];
    const TbFullscreenBatch *fs_batch =
        (const TbFullscreenBatch *)batch->user_batch;

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

void record_brightness(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Brightness Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Brightness", 3, true);
  cmd_begin_label(buffer, "Brightness", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  tb_record_fullscreen(buffer, batches,
                       (const TbFullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_bloom_blur(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDispatchBatch *batches) {
  TracyCZoneNC(ctx, "Bloom Blur Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Bloom Blur", 3, true);
  cmd_begin_label(buffer, "Bloom Blur", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDispatchBatch *batch = &batches[batch_idx];
    const TbFullscreenBatch *blur_batch =
        (const TbFullscreenBatch *)batch->user_batch;

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
                        uint32_t batch_count, const TbDrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }

  TracyCZoneNC(ctx, "Tonemapping Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Tonemapping", 3, true);
  cmd_begin_label(buffer, "Tonemapping", (float4){0.8f, 0.4f, 0.0f, 1.0f});

  tb_record_fullscreen(buffer, batches,
                       (const TbFullscreenBatch *)batches->user_batch);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void register_pass(TbRenderPipelineSystem *self, TbRenderThread *thread,
                   TbRenderPassId id, uint32_t *command_buffers,
                   uint32_t command_buffer_count) {
  TracyCZoneN(ctx, "Register Pass", true);
  TbRenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, id);
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    TbFrameState *state = &thread->frame_states[frame_idx];

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
        self->rt_sys, pass->attachments[0].layer, pass->attachments[0].mip,
        target_id);

    TbPassContext pass_context = (TbPassContext){
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
      TbImageTransition *barrier = &pass_context.barriers[trans_idx];
      *barrier = transition->barrier;
      barrier->barrier.image = tb_render_target_get_image(
          self->rt_sys, frame_idx, transition->render_target);
    }

    TB_DYN_ARR_APPEND(state->pass_contexts, pass_context);
  }
  TracyCZoneEnd(ctx);
}

typedef struct TbAttachmentInfo {
  VkClearValue clear_value;
  uint32_t layer;
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

TbRenderPassId create_render_pass(TbRenderPipelineSystem *self,
                                  const TbRenderPassCreateInfo *create_info) {
  TB_CHECK_RETURN(create_info, "Invalid Create Info ptr", InvalidRenderPassId);

  TbRenderPassId id = TB_DYN_ARR_SIZE(self->render_passes);
  TB_DYN_ARR_APPEND(self->render_passes, (TbRenderPass){0});
  TbRenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, id);

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
      pass->attachments[i] = (TbPassAttachment){
          .clear_value = attach_info->clear_value,
          .layer = attach_info->layer,
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
    TbRenderTargetSystem *rt_sys = self->rt_sys;
    // HACK: Assume all attachments have the same extents
    const VkExtent3D extent = tb_render_target_get_mip_extent(
        rt_sys, pass->attachments[0].layer, pass->attachments[0].mip,
        pass->attachments[0].attachment);

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      uint32_t color_count = 0;
      VkRenderingAttachmentInfo *depth_attachment = NULL;
      VkRenderingAttachmentInfo *stencil_attachment = NULL;
      VkRenderingAttachmentInfo *color_attachments = tb_alloc_nm_tp(
          self->gp_alloc, TB_MAX_ATTACHMENTS, VkRenderingAttachmentInfo);

      for (uint32_t rt_idx = 0; rt_idx < pass->attach_count; ++rt_idx) {
        const TbAttachmentInfo *attachment = &create_info->attachments[rt_idx];
        VkFormat format =
            tb_render_target_get_format(self->rt_sys, attachment->attachment);
        VkImageView view = tb_render_target_get_mip_view(
            self->rt_sys, attachment->layer, attachment->mip, i,
            attachment->attachment);

        VkRenderingAttachmentInfo *info = &color_attachments[color_count];
        VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (format == VK_FORMAT_D32_SFLOAT) {
          depth_attachment =
              tb_alloc_tp(self->gp_alloc, VkRenderingAttachmentInfo);
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

TbRenderPipelineSystem
create_render_pipeline_system(ecs_world_t *ecs, TbAllocator gp_alloc,
                              TbAllocator tmp_alloc, TbRenderSystem *rnd_sys,
                              TbRenderTargetSystem *rt_sys,
                              TbViewSystem *view_sys) {
  TbRenderPipelineSystem sys = {
      .rnd_sys = rnd_sys,
      .rt_sys = rt_sys,
      .view_sys = view_sys,
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
  };

  // Initialize the render pass array
  TB_DYN_ARR_RESET(sys.render_passes, sys.gp_alloc, 8);

  // Create some default passes
  {
    TracyCZoneN(ctx, "Create Default Passes", true);
    // Look up the render targets we know will be needed
    const TbRenderTargetId env_cube = rt_sys->env_cube;
    const TbRenderTargetId irradiance_map = rt_sys->irradiance_map;
    const TbRenderTargetId prefiltered_cube = rt_sys->prefiltered_cube;
    const TbRenderTargetId opaque_depth = rt_sys->depth_buffer;
    const TbRenderTargetId opaque_normal = rt_sys->normal_buffer;
    const TbRenderTargetId hdr_color = rt_sys->hdr_color;
    const TbRenderTargetId depth_copy = rt_sys->depth_buffer_copy;
    const TbRenderTargetId color_copy = rt_sys->color_copy;
    const TbRenderTargetId swapchain_target = rt_sys->swapchain;
    const TbRenderTargetId transparent_depth = rt_sys->depth_buffer;
    const TbRenderTargetId shadow_map = rt_sys->shadow_map;
    const TbRenderTargetId brightness = rt_sys->brightness;

    // Create opaque depth normal pass
    {
      const uint32_t trans_count = 4;
      TbRenderPassCreateInfo create_info = {
          .transition_count = trans_count,
          .transitions =
              (PassTransition[trans_count]){
                  {
                      .render_target = sys.rt_sys->depth_buffer,
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
                      .render_target = sys.rt_sys->normal_buffer,
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
                  // We know we can fit some extra transitions here
                  {
                      .render_target = sys.rt_sys->hdr_color,
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
                      .render_target = rt_sys->ldr_target,
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId,
               "Failed to create opaque depth normal pass");
      sys.opaque_depth_normal_pass = id;
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
            .dependencies = (TbRenderPassId[1]){sys.opaque_depth_normal_pass},
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

        TbRenderPassId id = create_render_pass(&sys, &create_info);
        TB_CHECK(id != InvalidRenderPassId,
                 "Failed to create env capture pass");
        sys.env_cap_passes[i] = id;
      }
    }
    // Create irradiance convolution pass
    {
      TbRenderPassCreateInfo create_info = {
          .view_mask = 0x0000003F, // 0b00111111
          .dependency_count = 1,
          .dependencies =
              (TbRenderPassId[1]){sys.env_cap_passes[PREFILTER_PASS_COUNT - 1]},
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
                      .render_target = sys.rt_sys->irradiance_map,
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create irradiance pass");
      sys.irradiance_pass = id;
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
              .render_target = sys.rt_sys->prefiltered_cube,
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
            .dependencies = (TbRenderPassId[1]){sys.irradiance_pass},
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

        TbRenderPassId id = create_render_pass(&sys, &create_info);
        TB_CHECK(id != InvalidRenderPassId, "Failed to create prefilter pass");
        sys.prefilter_passes[i] = id;
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
              .render_target = sys.rt_sys->shadow_map,
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
                                      .layerCount = TB_CASCADE_COUNT,
                                  },
                          },
                  },
          },
      };

      for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
           ++cascade_idx) {
        TbRenderPassCreateInfo create_info = {
            .dependency_count = 1,
            .dependencies = (TbRenderPassId[1]){sys.opaque_depth_normal_pass},
            .attachment_count = 1,
            .attachments =
                (TbAttachmentInfo[1]){
                    {
                        .clear_value = {.depthStencil = {.depth = 1.0f,
                                                         .stencil = 0u}},
                        .layer = cascade_idx,
                        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .attachment = shadow_map,
                    },
                },
            .name = "Shadow Pass",
        };
        // Only need to schedule transitions for the first pass
        if (cascade_idx == 0) {
          create_info.transition_count = trans_count;
          create_info.transitions = transitions;
        }

        TbRenderPassId id = create_render_pass(&sys, &create_info);
        TB_CHECK(id != InvalidRenderPassId, "Failed to create shadow pass");
        sys.shadow_passes[cascade_idx] = id;
      }
    }
    // Create opaque color pass
    {
      // Transition irradiance map, prefiltered env map and shadow map
      PassTransition irr_trans = {
          .render_target = sys.rt_sys->irradiance_map,
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
          .render_target = sys.rt_sys->prefiltered_cube,
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
      PassTransition depth_trans = {
          .render_target = sys.rt_sys->depth_buffer,
          .barrier =
              {
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
                          .subresourceRange =
                              {
                                  .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                  .levelCount = 1,
                                  .layerCount = 1,
                              },
                      },
              },
      };
      PassTransition shadow_trans = {
          .render_target = sys.rt_sys->shadow_map,
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
                              .layerCount = TB_CASCADE_COUNT,
                          },
                  },
          }};
      const uint32_t transition_count = 4;
      PassTransition transitions[4] = {shadow_trans, irr_trans, filter_trans,
                                       depth_trans};

      TbRenderPassCreateInfo create_info = {
          .dependency_count = 2,
          .dependencies =
              (TbRenderPassId[2]){sys.opaque_depth_normal_pass,
                                  sys.shadow_passes[TB_CASCADE_COUNT - 1]},
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create opaque color pass");
      sys.opaque_color_pass = id;
    }
    // Create sky pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 2,
          .dependencies = (TbRenderPassId[2]){sys.opaque_depth_normal_pass,
                                              sys.opaque_color_pass},
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create sky pass");
      sys.sky_pass = id;
    }
    // Create opaque depth copy pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.sky_pass},
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
                  {
                      .render_target = sys.rt_sys->depth_buffer_copy,
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create depth copy pass");
      sys.depth_copy_pass = id;
    }
    // Create opaque color copy pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.depth_copy_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  {
                      .render_target = sys.rt_sys->hdr_color,
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
                      .render_target = sys.rt_sys->color_copy,
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create color copy pass");
      sys.color_copy_pass = id;
    }
    // Create transparent depth pass
    {
      // Must transition back to depth so that we can load the contents
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.color_copy_pass},
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
                  {
                      .render_target = sys.rt_sys->depth_buffer,
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId,
               "Failed to create transparent depth pass");
      sys.transparent_depth_pass = id;
    }
    // Create transparent color pass
    {
      PassTransition transitions[3] = {
          {
              .render_target = sys.rt_sys->hdr_color,
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
              .render_target = sys.rt_sys->color_copy,
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
              .render_target = sys.rt_sys->depth_buffer_copy,
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
          .dependencies = (TbRenderPassId[1]){sys.transparent_depth_pass},
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId,
               "Failed to create transparent color pass");
      sys.transparent_color_pass = id;
    }
    // Create brightness pass
    {
      static const size_t trans_count = 2;
      PassTransition transitions[2] = {
          {
              .render_target = sys.rt_sys->hdr_color,
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
              .render_target = sys.rt_sys->brightness,
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
          .dependencies = (TbRenderPassId[1]){sys.transparent_color_pass},
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

      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId,
               "Failed to create brightness downsample pass");
      sys.brightness_pass = id;
    }
    // Create luminance compute pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.brightness_pass},
          .name = "Luminance Pass",
      };
      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create luminance pass");
      sys.luminance_pass = id;
    }
    // Create one pass for downsampling
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.luminance_pass},
          .transition_count = 2,
          .transitions =
              (PassTransition[2]){
                  // Need to read brightness
                  {
                      .render_target = sys.rt_sys->brightness,
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
                      .render_target = sys.rt_sys->bloom_mip_chain,
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
      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId,
               "Failed to create bloom downsample pass");
      sys.bloom_downsample_pass = id;
    }
    // And one for upsampling
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.bloom_downsample_pass},
          .name = "Bloom Upsample",
      };
      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId,
               "Failed to create bloom upsample pass");
      sys.bloom_upsample_pass = id;
    }
    // Create tonemapping pass
    {
      const uint32_t trans_count = 1;
      // Need to read bloom mip chain (mip 0 only)
      PassTransition transitions[1] = {
          {
              .render_target = sys.rt_sys->bloom_mip_chain,
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
          .dependencies = (TbRenderPassId[1]){sys.bloom_upsample_pass},
          .transition_count = trans_count,
          .transitions = transitions,
          .attachment_count = 1,
          .attachments =
              (TbAttachmentInfo[1]){
                  {
                      .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                      .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                      .attachment = rt_sys->ldr_target,
                  },
              },
          .name = "Tonemapping Pass",
      };
      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create tonemap pass");
      sys.tonemap_pass = id;
    }
    // Create anti-aliasing pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.tonemap_pass},
          .transition_count = 1,
          .transitions =
              (PassTransition[1]){
                  {
                      .render_target = rt_sys->ldr_target,
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
          .name = "FXAA Pass",
      };
      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create fxaa pass");
      sys.fxaa_pass = id;
    }
    // Create UI Pass
    {
      TbRenderPassCreateInfo create_info = {
          .dependency_count = 1,
          .dependencies = (TbRenderPassId[1]){sys.fxaa_pass},
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
      TbRenderPassId id = create_render_pass(&sys, &create_info);
      TB_CHECK(id != InvalidRenderPassId, "Failed to create ui pass");
      sys.ui_pass = id;
    }

    TracyCZoneEnd(ctx);
  }

  // Calculate pass order
  const uint32_t pass_count = TB_DYN_ARR_SIZE(sys.render_passes);
  sys.pass_order = tb_alloc_nm_tp(sys.gp_alloc, pass_count, uint32_t);

  sort_pass_graph(&sys);

  // Once we've sorted passes, go through the passes
  // in execution order and determine where full pipelines are used.
  // Every time we return to the top of the pipeline, we want to keep track
  // so we can use a different command buffer.
  {
    TracyCZoneN(ctx, "Register Passes", true);
    uint32_t command_buffer_count = 0; // Treated as an index while builiding
    // Worst case each pass needs its own command buffer
    uint32_t *command_buffer_indices =
        tb_alloc_nm_tp(sys.tmp_alloc, pass_count, uint32_t);

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
        const uint32_t idx = sys.pass_order[pass_idx];
        register_pass(&sys, sys.rnd_sys->render_thread, idx,
                      command_buffer_indices, command_buffer_count);
      }
    }
    TracyCZoneEnd(ctx);
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
        err = tb_rnd_create_sampler(rnd_sys, &create_info, "Copy Sampler",
                                    &sys.sampler);
        TB_VK_CHECK(err, "Failed to create copy sampler");
      }
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
        err = tb_rnd_create_sampler(rnd_sys, &create_info, "Noise Sampler",
                                    &sys.noise_sampler);
        TB_VK_CHECK(err, "Failed to create noise sampler");
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
                    .pImmutableSamplers = &sys.sampler,
                },
            }};
        err = tb_rnd_create_set_layout(rnd_sys, &create_info,
                                       "Copy Descriptor Set Layout",
                                       &sys.copy_set_layout);
        TB_VK_CHECK(err, "Failed to create copy descriptor set layout");
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
                    .pImmutableSamplers = &sys.sampler,
                },
            }};
        err = tb_rnd_create_set_layout(rnd_sys, &create_info,
                                       "Compute Copy Descriptor Set Layout",
                                       &sys.comp_copy_set_layout);
        TB_VK_CHECK(err, "Failed to create compute copy descriptor set layout");
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
                    .pImmutableSamplers = &sys.sampler,
                },
            }};
        err = tb_rnd_create_set_layout(rnd_sys, &create_info,
                                       "Tonemap Descriptor Set Layout",
                                       &sys.tonemap_set_layout);
        TB_VK_CHECK(err, "Failed to create tonemap set layout");
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts =
                (VkDescriptorSetLayout[1]){
                    sys.copy_set_layout,
                },
        };
        err = tb_rnd_create_pipeline_layout(sys.rnd_sys, &create_info,
                                            "Copy Pipeline Layout",
                                            &sys.copy_pipe_layout);
        TB_VK_CHECK(err, "Failed to create copy pipeline layout");
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts =
                (VkDescriptorSetLayout[1]){
                    sys.tonemap_set_layout,
                },
        };
        err = tb_rnd_create_pipeline_layout(sys.rnd_sys, &create_info,
                                            "Tonemap Pipeline Layout",
                                            &sys.tonemap_pipe_layout);
        TB_VK_CHECK(err, "Failed to create tonemap pipeline layout");
      }

      {
        VkPipelineLayoutCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = (VkDescriptorSetLayout[1]){sys.comp_copy_set_layout},
        };
        err = tb_rnd_create_pipeline_layout(sys.rnd_sys, &create_info,
                                            "Comp Copy Pipeline Layout",
                                            &sys.comp_copy_pipe_layout);
        TB_VK_CHECK(err, "Failed to create compute copy pipeline layout");
      }

      {
        uint32_t attach_count = 0;
        tb_render_pipeline_get_attachments(&sys, sys.depth_copy_pass,
                                           &attach_count, NULL);
        TB_CHECK(attach_count == 1, "Unexpected");
        TbPassAttachment depth_info = {0};
        tb_render_pipeline_get_attachments(&sys, sys.depth_copy_pass,
                                           &attach_count, &depth_info);

        TbPipeShaderArgs args = {
            .rnd_sys = rnd_sys,
            .format =
                tb_render_target_get_format(sys.rt_sys, depth_info.attachment),
            .pipe_layout = sys.copy_pipe_layout,
        };
        sys.depth_copy_shader = tb_shader_load(ecs, create_depth_pipeline,
                                               &args, sizeof(TbPipeShaderArgs));
      }

      {
        TbDrawContextDescriptor desc = {
            .batch_size = sizeof(TbFullscreenBatch),
            .draw_fn = record_depth_copy,
            .pass_id = sys.depth_copy_pass,
        };
        sys.depth_copy_ctx =
            tb_render_pipeline_register_draw_context(&sys, &desc);
        TB_CHECK(sys.depth_copy_ctx != InvalidDrawContextId,
                 "Failed to create depth copy draw context");
      }
    }

    // Color Copy
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(&sys, sys.color_copy_pass,
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      TbPassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(&sys, sys.color_copy_pass,
                                         &attach_count, &attach_info);

      TbPipeShaderArgs args = {
          .rnd_sys = rnd_sys,
          .format =
              tb_render_target_get_format(sys.rt_sys, attach_info.attachment),
          .pipe_layout = sys.copy_pipe_layout,
      };
      sys.color_copy_shader = tb_shader_load(ecs, create_color_copy_pipeline,
                                             &args, sizeof(TbPipeShaderArgs));

      {
        TbDrawContextDescriptor desc = {
            .batch_size = sizeof(TbFullscreenBatch),
            .draw_fn = record_color_copy,
            .pass_id = sys.color_copy_pass,
        };
        sys.color_copy_ctx =
            tb_render_pipeline_register_draw_context(&sys, &desc);
        TB_CHECK(sys.color_copy_ctx != InvalidDrawContextId,
                 "Failed to create color copy draw context");
      }
    }

    // Compute Copy
    {
      TbPipeShaderArgs args = {
          .rnd_sys = rnd_sys,
          .pipe_layout = sys.comp_copy_pipe_layout,
      };
      sys.comp_copy_shader = tb_shader_load(ecs, create_comp_copy_pipeline,
                                            &args, sizeof(TbPipeShaderArgs));

      // Contexts for specific copy operations
      TbDispatchContextDescriptor desc = {
          .batch_size = sizeof(TbFullscreenBatch),
          .dispatch_fn = record_comp_copy,
          .pass_id = sys.bloom_blur_pass,
      };
      sys.bloom_copy_ctx =
          tb_render_pipeline_register_dispatch_context(&sys, &desc);
      TB_CHECK(sys.bloom_copy_ctx != InvalidDispatchContextId,
               "Failed to create compute copy dispatch context");
    }

    // Create bloom work
    err = tb_create_downsample_work(ecs, sys.rnd_sys, &sys, sys.sampler,
                                    sys.bloom_downsample_pass,
                                    &sys.downsample_work);
    err = tb_create_upsample_work(ecs, sys.rnd_sys, &sys, sys.sampler,
                                  sys.bloom_upsample_pass, &sys.upsample_work);

    // Compute Luminance Histogram and Average work
    tb_create_lum_hist_work(ecs, sys.rnd_sys, &sys, sys.sampler,
                            sys.luminance_pass, &sys.lum_hist_work);
    tb_create_lum_avg_work(ecs, sys.rnd_sys, &sys, sys.luminance_pass,
                           &sys.lum_avg_work);

    // Brightness
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(&sys, sys.brightness_pass,
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      TbPassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(&sys, sys.brightness_pass,
                                         &attach_count, &attach_info);

      TbPipeShaderArgs args = {
          .rnd_sys = rnd_sys,
          .format =
              tb_render_target_get_format(sys.rt_sys, attach_info.attachment),
          .pipe_layout = sys.copy_pipe_layout,
      };
      sys.brightness_shader = tb_shader_load(ecs, create_brightness_pipeline,
                                             &args, sizeof(TbPipeShaderArgs));

      TbDrawContextDescriptor desc = {
          .batch_size = sizeof(TbFullscreenBatch),
          .draw_fn = record_brightness,
          .pass_id = sys.brightness_pass,
      };
      sys.brightness_ctx =
          tb_render_pipeline_register_draw_context(&sys, &desc);
      TB_CHECK(sys.brightness_ctx != InvalidDrawContextId,
               "Failed to create brightness draw context");
    }

    // Blur
    {
      TbDispatchContextDescriptor desc = {
          .batch_size = sizeof(TbFullscreenBatch),
          .dispatch_fn = record_bloom_blur,
          .pass_id = sys.bloom_blur_pass,
      };
      sys.bloom_blur_ctx =
          tb_render_pipeline_register_dispatch_context(&sys, &desc);
      TB_CHECK(sys.bloom_blur_ctx != InvalidDispatchContextId,
               "Failed to create bloom blur dispatch context");
    }

    // Tonemapping
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(&sys, sys.tonemap_pass, &attach_count,
                                         NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      TbPassAttachment attach_info = {0};
      tb_render_pipeline_get_attachments(&sys, sys.tonemap_pass, &attach_count,
                                         &attach_info);

      TbPipeShaderArgs args = {
          .rnd_sys = rnd_sys,
          .format =
              tb_render_target_get_format(sys.rt_sys, attach_info.attachment),
          .pipe_layout = sys.tonemap_pipe_layout,
      };
      sys.tonemap_shader = tb_shader_load(ecs, create_tonemapping_pipeline,
                                          &args, sizeof(TbPipeShaderArgs));

      TbDrawContextDescriptor desc = {
          .batch_size = sizeof(TbFullscreenBatch),
          .draw_fn = record_tonemapping,
          .pass_id = sys.tonemap_pass,
      };
      sys.tonemap_ctx = tb_render_pipeline_register_draw_context(&sys, &desc);
      TB_CHECK(sys.tonemap_ctx != InvalidDrawContextId,
               "Failed to create tonemapping draw context");
    }
  }

  return sys;
}

void destroy_render_pipeline_system(ecs_world_t *ecs,
                                    TbRenderPipelineSystem *self) {
  tb_rnd_destroy_sampler(self->rnd_sys, self->sampler);
  tb_rnd_destroy_sampler(self->rnd_sys, self->noise_sampler);
  tb_rnd_destroy_set_layout(self->rnd_sys, self->copy_set_layout);
  tb_rnd_destroy_set_layout(self->rnd_sys, self->comp_copy_set_layout);
  tb_rnd_destroy_set_layout(self->rnd_sys, self->tonemap_set_layout);
  tb_rnd_destroy_pipe_layout(self->rnd_sys, self->copy_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->rnd_sys, self->comp_copy_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->rnd_sys, self->tonemap_pipe_layout);
  tb_shader_destroy(ecs, self->depth_copy_shader);
  tb_shader_destroy(ecs, self->color_copy_shader);
  tb_shader_destroy(ecs, self->comp_copy_shader);
  tb_shader_destroy(ecs, self->brightness_shader);
  tb_shader_destroy(ecs, self->tonemap_shader);

  tb_destroy_downsample_work(ecs, self->rnd_sys, &self->downsample_work);
  tb_destroy_upsample_work(ecs, self->rnd_sys, &self->upsample_work);

  tb_destroy_lum_avg_work(ecs, self->rnd_sys, &self->lum_avg_work);
  tb_destroy_lum_hist_work(ecs, self->rnd_sys, &self->lum_hist_work);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->rnd_sys,
                                   self->descriptor_pools[i].set_pool);
    tb_rnd_destroy_descriptor_pool(self->rnd_sys,
                                   self->down_desc_pools[i].set_pool);
    tb_rnd_destroy_descriptor_pool(self->rnd_sys,
                                   self->up_desc_pools[i].set_pool);
  }

  TB_DYN_ARR_DESTROY(self->render_passes);
  tb_free(self->gp_alloc, self->pass_order);

  *self = (TbRenderPipelineSystem){0};
}

void tick_core_desc_pool(TbRenderPipelineSystem *self) {
  VkResult err = VK_SUCCESS;
  const uint32_t set_count = 5;
  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = set_count * 4,
      .poolSizeCount = 3,
      .pPoolSizes =
          (VkDescriptorPoolSize[3]){
              {
                  .descriptorCount = set_count * 4,
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              },
              {
                  .descriptorCount = set_count * 4,
                  .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              },
              {
                  .descriptorCount = set_count * 4,
                  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              },
          },
  };
  VkDescriptorSetLayout layouts[set_count] = {
      self->copy_set_layout,          self->copy_set_layout,
      self->lum_hist_work.set_layout, self->lum_avg_work.set_layout,
      self->tonemap_set_layout,
  };
  err = tb_rnd_frame_desc_pool_tick(
      self->rnd_sys, "render_pipeline", &pool_info, layouts, NULL,
      self->descriptor_pools, set_count, set_count);
  TB_VK_CHECK(err, "Failed to tick descriptor pool");
#undef SET_COUNT

  VkDescriptorSet depth_set =
      tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->descriptor_pools, 0);
  VkDescriptorSet color_set =
      tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->descriptor_pools, 1);
  VkDescriptorSet lum_hist_set =
      tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->descriptor_pools, 2);
  VkDescriptorSet lum_avg_set =
      tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->descriptor_pools, 3);
  VkDescriptorSet tonemap_set =
      tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->descriptor_pools, 4);

  VkImageView depth_view = tb_render_target_get_view(
      self->rt_sys, self->rnd_sys->frame_idx, self->rt_sys->depth_buffer);
  VkImageView color_view = tb_render_target_get_view(
      self->rt_sys, self->rnd_sys->frame_idx, self->rt_sys->hdr_color);
  VkBuffer lum_hist_buffer = self->lum_hist_work.lum_histogram.buffer;
  VkBuffer lum_avg_buffer = self->lum_avg_work.lum_avg.buffer;
  VkImageView bloom_full_view = tb_render_target_get_mip_view(
      self->rt_sys, 0, 0, self->rnd_sys->frame_idx,
      self->rt_sys->bloom_mip_chain);

#define WRITE_COUNT 9
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
  tb_rnd_update_descriptors(self->rnd_sys, WRITE_COUNT, writes);
#undef WRITE_COUNT
}

void tick_downsample_desc_pool(TbRenderPipelineSystem *self) {
  VkResult err = VK_SUCCESS;

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = BLUR_BATCH_COUNT * 4,
      .poolSizeCount = 2,
      .pPoolSizes =
          (VkDescriptorPoolSize[2]){
              {
                  .descriptorCount = BLUR_BATCH_COUNT * 4,
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              },
              {
                  .descriptorCount = BLUR_BATCH_COUNT * 4,
                  .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              },
          },
  };
  VkDescriptorSetLayout layouts[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    layouts[i] = self->downsample_work.set_layout;
  }

  err = tb_rnd_frame_desc_pool_tick(self->rnd_sys, "downsample", &pool_info,
                                    layouts, NULL, self->down_desc_pools,
                                    BLUR_BATCH_COUNT, BLUR_BATCH_COUNT);
  TB_VK_CHECK(err, "Failed to tick descriptor pool");

#define WRITE_COUNT BLUR_BATCH_COUNT * 2
  VkDescriptorSet sets[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    sets[i] =
        tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->down_desc_pools, i);
  }
  VkImageView input[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    if (i == 0) {
      // First input is always the brightness target
      input[i] = tb_render_target_get_view(
          self->rt_sys, self->rnd_sys->frame_idx, self->rt_sys->brightness);
    } else {
      input[i] = tb_render_target_get_mip_view(self->rt_sys, 0, i,
                                               self->rnd_sys->frame_idx,
                                               self->rt_sys->bloom_mip_chain);
    }
  }
  VkImageView output[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    output[i] = tb_render_target_get_mip_view(self->rt_sys, 0, i + 1,
                                              self->rnd_sys->frame_idx,
                                              self->rt_sys->bloom_mip_chain);
  }

  VkWriteDescriptorSet writes[WRITE_COUNT] = {0};
  int32_t set_idx = 0;
  for (int32_t i = 0; i < WRITE_COUNT; i += 2) {
    VkDescriptorSet set = sets[set_idx];
    VkDescriptorImageInfo *input_info =
        tb_alloc_tp(self->tmp_alloc, VkDescriptorImageInfo);
    *input_info = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .imageView = input[set_idx],
    };
    VkDescriptorImageInfo *output_info =
        tb_alloc_tp(self->tmp_alloc, VkDescriptorImageInfo);
    *output_info = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .imageView = output[set_idx],
    };
    writes[i] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = input_info,

    };
    writes[i + 1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = output_info,
    };
    set_idx++;
  }
  tb_rnd_update_descriptors(self->rnd_sys, WRITE_COUNT, writes);
#undef WRITE_COUNT
}

void tick_upsample_desc_pool(TbRenderPipelineSystem *self) {
  VkResult err = VK_SUCCESS;

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = BLUR_BATCH_COUNT * 4,
      .poolSizeCount = 2,
      .pPoolSizes =
          (VkDescriptorPoolSize[2]){
              {
                  .descriptorCount = BLUR_BATCH_COUNT * 4,
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              },
              {
                  .descriptorCount = BLUR_BATCH_COUNT * 4,
                  .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              },
          },
  };
  VkDescriptorSetLayout layouts[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    layouts[i] = self->upsample_work.set_layout;
  }

  err = tb_rnd_frame_desc_pool_tick(self->rnd_sys, "upsample", &pool_info,
                                    layouts, NULL, self->up_desc_pools,
                                    BLUR_BATCH_COUNT, BLUR_BATCH_COUNT);
  TB_VK_CHECK(err, "Failed to tick descriptor pool");

#define WRITE_COUNT BLUR_BATCH_COUNT * 2
  VkDescriptorSet sets[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    sets[i] =
        tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->up_desc_pools, i);
  }
  VkImageView input[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    input[i] = tb_render_target_get_mip_view(
        self->rt_sys, 0, BLUR_BATCH_COUNT - i, self->rnd_sys->frame_idx,
        self->rt_sys->bloom_mip_chain);
  }
  VkImageView output[BLUR_BATCH_COUNT] = {0};
  for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
    output[i] = tb_render_target_get_mip_view(
        self->rt_sys, 0, BLUR_BATCH_COUNT - i - 1, self->rnd_sys->frame_idx,
        self->rt_sys->bloom_mip_chain);
  }

  VkWriteDescriptorSet writes[WRITE_COUNT] = {0};
  int32_t set_idx = 0;
  for (int32_t i = 0; i < WRITE_COUNT; i += 2) {
    VkDescriptorSet set = sets[set_idx];
    VkDescriptorImageInfo *input_info =
        tb_alloc_tp(self->tmp_alloc, VkDescriptorImageInfo);
    *input_info = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .imageView = input[set_idx],
    };
    VkDescriptorImageInfo *output_info =
        tb_alloc_tp(self->tmp_alloc, VkDescriptorImageInfo);
    *output_info = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .imageView = output[set_idx],
    };
    writes[i] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = input_info,
    };
    writes[i + 1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = output_info,
    };
    set_idx++;
  }
  tb_rnd_update_descriptors(self->rnd_sys, WRITE_COUNT, writes);
#undef WRITE_COUNT
}

void tick_render_pipeline_sys(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Render Pipeline System Tick", TracyCategoryColorRendering,
               true);

  tb_auto self = ecs_field(it, TbRenderPipelineSystem, 1);

  tb_auto brdf_tex = tb_get_brdf_tex(it->world);

  // A few passes will be driven from here because an external system
  // has no need to directly drive these passes

  // Handle descriptor set writes
  tick_core_desc_pool(self);
  tick_downsample_desc_pool(self);
  tick_upsample_desc_pool(self);

  // Issue draws for full screen passes
  {
    VkDescriptorSet depth_set = tb_rnd_frame_desc_pool_get_set(
        self->rnd_sys, self->descriptor_pools, 0);
    VkDescriptorSet color_set = tb_rnd_frame_desc_pool_get_set(
        self->rnd_sys, self->descriptor_pools, 1);
    VkDescriptorSet lum_hist_set = tb_rnd_frame_desc_pool_get_set(
        self->rnd_sys, self->descriptor_pools, 2);
    VkDescriptorSet lum_avg_set = tb_rnd_frame_desc_pool_get_set(
        self->rnd_sys, self->descriptor_pools, 3);
    VkDescriptorSet tonemap_set = tb_rnd_frame_desc_pool_get_set(
        self->rnd_sys, self->descriptor_pools, 4);

    VkDescriptorSet downsample_sets[BLUR_BATCH_COUNT] = {0};
    VkDescriptorSet upsample_sets[BLUR_BATCH_COUNT] = {0};
    for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
      downsample_sets[i] = tb_rnd_frame_desc_pool_get_set(
          self->rnd_sys, self->down_desc_pools, i);
      upsample_sets[i] =
          tb_rnd_frame_desc_pool_get_set(self->rnd_sys, self->up_desc_pools, i);
    }

    // TODO: Make this less hacky
    const uint32_t width = self->rnd_sys->render_thread->swapchain.width;
    const uint32_t height = self->rnd_sys->render_thread->swapchain.height;

    // Depth copy pass
    if (tb_is_shader_ready(it->world, self->color_copy_shader)) {
      TbFullscreenBatch fs_batch = {
          .set = depth_set,
      };
      TbDrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline =
              tb_shader_get_pipeline(it->world, self->depth_copy_shader),
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->depth_copy_ctx, 1,
                                          &batch);
    }
    // Color copy pass
    if (tb_is_shader_ready(it->world, self->color_copy_shader)) {
      TbFullscreenBatch fs_batch = {
          .set = color_set,
      };
      TbDrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline =
              tb_shader_get_pipeline(it->world, self->color_copy_shader),
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->color_copy_ctx, 1,
                                          &batch);
    }
    if (tb_is_shader_ready(it->world, self->lum_hist_work.shader) &&
        tb_is_shader_ready(it->world, self->lum_avg_work.shader)) {
      // Configurables
      float min_log_lum = -5.0f;
      float max_log_lum = 10.0f;
      // Luminance histogram gather pass
      {
        uint32_t group_x = (uint32_t)SDL_ceilf((float)width / 16.0f);
        uint32_t group_y = (uint32_t)SDL_ceilf((float)height / 16.0f);
        TbLuminanceBatch lum_batch = {
            .set = lum_hist_set,
            .consts = {.params = {min_log_lum, 1 / (max_log_lum - min_log_lum),
                                  (float)width, (float)height}},
        };
        TbDispatchBatch batch = {
            .layout = self->lum_hist_work.pipe_layout,
            .pipeline =
                tb_shader_get_pipeline(it->world, self->lum_hist_work.shader),
            .user_batch = &lum_batch,
            .group_count = 1,
            .groups[0] = {group_x, group_y, 1},
        };
        tb_render_pipeline_issue_dispatch_batch(self, self->lum_hist_work.ctx,
                                                1, &batch);
      }
      // Luminance average pass
      if (tb_is_shader_ready(it->world, self->lum_avg_work.shader)) {
        float time = tb_clampf(1.f - SDL_expf(-it->delta_time * 1.1f), 0, 1);
        TbLuminanceBatch lum_batch = {
            .set = lum_avg_set,
            .consts = {.params = {min_log_lum, (max_log_lum - min_log_lum),
                                  time, (float)width * (float)height}},
        };
        TbDispatchBatch batch = {
            .layout = self->lum_avg_work.pipe_layout,
            .pipeline =
                tb_shader_get_pipeline(it->world, self->lum_avg_work.shader),
            .user_batch = &lum_batch,
            .group_count = 1,
            .groups[0] = {1, 1, 1},
        };
        tb_render_pipeline_issue_dispatch_batch(self, self->lum_avg_work.ctx, 1,
                                                &batch);
      }
    }
    // Brightness pass
    if (tb_is_shader_ready(it->world, self->brightness_shader) &&
        tb_is_texture_ready(it->world, brdf_tex)) {
      TbFullscreenBatch fs_batch = {
          .set = color_set,
      };
      TbDrawBatch batch = {
          .layout = self->copy_pipe_layout,
          .pipeline =
              tb_shader_get_pipeline(it->world, self->brightness_shader),
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->brightness_ctx, 1,
                                          &batch);
    }
    // Blur passes
    if (tb_is_shader_ready(it->world, self->downsample_work.shader) &&
        tb_is_shader_ready(it->world, self->upsample_work.shader)) {
      DownsampleBatch downsample_batches[BLUR_BATCH_COUNT] = {0};
      TbDispatchBatch down_batches[BLUR_BATCH_COUNT] = {0};
      for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {

        uint32_t group_width = (width / 16) + 1;
        uint32_t group_height = (height / 16) + 1;
        if (i != 0) {
          group_width = ((width / (i * 2)) / 16) + 1;
          group_height = ((height / (i * 2)) / 16) + 1;
        }
        downsample_batches[i] = (DownsampleBatch){.set = downsample_sets[i]};
        down_batches[i] = (TbDispatchBatch){
            .layout = self->downsample_work.pipe_layout,
            .pipeline =
                tb_shader_get_pipeline(it->world, self->downsample_work.shader),
            .user_batch = &downsample_batches[i],
            .group_count = 1,
            .groups[0] = {group_width, group_height, 1},
        };
      }
      tb_render_pipeline_issue_dispatch_batch(self, self->downsample_work.ctx,
                                              BLUR_BATCH_COUNT, down_batches);
      UpsampleBatch upsample_batches[BLUR_BATCH_COUNT] = {0};
      TbDispatchBatch up_batches[BLUR_BATCH_COUNT] = {0};
      for (int32_t i = 0; i < BLUR_BATCH_COUNT; ++i) {
        uint32_t g = BLUR_BATCH_COUNT - (i + 1);

        uint32_t group_width = (width / 16) + 1;
        uint32_t group_height = (height / 16) + 1;
        if (g != 0) {
          group_width = ((width / (g * 2)) / 16) + 1;
          group_height = ((height / (g * 2)) / 16) + 1;
        }
        upsample_batches[i] = (UpsampleBatch){.set = upsample_sets[i]};
        up_batches[i] = (TbDispatchBatch){
            .layout = self->upsample_work.pipe_layout,
            .pipeline =
                tb_shader_get_pipeline(it->world, self->upsample_work.shader),
            .user_batch = &upsample_batches[i],
            .group_count = 1,
            .groups[0] = {group_width, group_height, 1},
        };
      }
      tb_render_pipeline_issue_dispatch_batch(self, self->upsample_work.ctx,
                                              BLUR_BATCH_COUNT, up_batches);
#undef BLUR_BATCH_COUNT
    }

    // Tonemapping pass
    if (tb_is_shader_ready(it->world, self->downsample_work.shader)) {
      TbFullscreenBatch fs_batch = {
          .set = tonemap_set,
      };
      TbDrawBatch batch = {
          .layout = self->tonemap_pipe_layout,
          .pipeline = tb_shader_get_pipeline(it->world, self->tonemap_shader),
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = &fs_batch,
      };
      tb_render_pipeline_issue_draw_batch(self, self->tonemap_ctx, 1, &batch);
    }
  }

  TracyCZoneEnd(ctx);
}

void rp_check_swapchain_resize(ecs_iter_t *it) {
  TbRenderPipelineSystem *rp_sys = ecs_field(it, TbRenderPipelineSystem, 1);
  TbRenderSystem *rnd_sys = rp_sys->rnd_sys;
  if (rnd_sys->render_thread->swapchain_resize_signal) {
    TracyCZoneN(resize_ctx, "Resize", true);

    for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
      tb_auto frame_state = &rnd_sys->render_thread->frame_states[frame_idx];
      // GPU must be finished with resources before we resize
      VkDevice device = rnd_sys->render_thread->device;
      if (vkGetFenceStatus(device, frame_state->fence) == VK_NOT_READY) {
        vkWaitForFences(device, 1, &frame_state->fence, VK_TRUE,
                        SDL_MAX_UINT64);
      }
      // Clear out any in flight descriptor updates since this resize will
      // invalidate them
      TB_QUEUE_CLEAR(*frame_state->set_write_queue);
    }

    tb_rnd_on_swapchain_resize(rp_sys);

    // Let the render thread know we're done handling the resize on the
    // main thread
    SDL_PostSemaphore(rnd_sys->render_thread->resized);

    // Reset back to frame 0
    rnd_sys->frame_idx = 0;

    TracyCZoneEnd(resize_ctx);
  }
}

void tb_register_render_pipeline_sys(TbWorld *world) {
  TracyCZoneNC(ctx, "Register Render Pipeline Sys", TracyCategoryColorRendering,
               true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbRenderPipelineSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto rt_sys = ecs_singleton_get_mut(ecs, TbRenderTargetSystem);
  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  TbRenderPipelineSystem sys = create_render_pipeline_system(
      world->ecs, world->gp_alloc, world->tmp_alloc, rnd_sys, rt_sys, view_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbRenderPipelineSystem), TbRenderPipelineSystem,
              &sys);

  ECS_SYSTEM(ecs, rp_check_swapchain_resize, EcsPreFrame,
             TbRenderPipelineSystem(TbRenderPipelineSystem));

  ECS_SYSTEM(ecs, tick_render_pipeline_sys, EcsPostUpdate,
             TbRenderPipelineSystem(TbRenderPipelineSystem));
  TracyCZoneEnd(ctx);
}

void tb_unregister_render_pipeline_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  tb_auto sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  destroy_render_pipeline_system(ecs, sys);
  ecs_singleton_remove(ecs, TbRenderPipelineSystem);
}

void reimport_render_pass(TbRenderPipelineSystem *self, TbRenderPassId id) {
  TbRenderPass *rp = &TB_DYN_ARR_AT(self->render_passes, id);

  {
    TbRenderTargetSystem *rt_sys = self->rt_sys;
    // HACK: Assume all attachments have the same extents
    const VkExtent3D extent = tb_render_target_get_mip_extent(
        rt_sys, rp->attachments[0].layer, rp->attachments[0].mip,
        rp->attachments[0].attachment);

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      // Update the pass context on each frame index
      {
        TbFrameState *state = &self->rnd_sys->render_thread->frame_states[i];
        TbPassContext *context = &TB_DYN_ARR_AT(state->pass_contexts, id);
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
              rt_sys, rp->attachments[attach_idx].layer,
              rp->attachments[attach_idx].mip, i, rt);

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

void tb_rnd_on_swapchain_resize(TbRenderPipelineSystem *self) {
  // Called by the core system as a hack when the swapchain resizes
  // This is where, on the main thread, we have to adjust to any render passes
  // and render targets to stay up to date with the latest swapchain

  // Reimport the swapchain target and resize all default targets
  // The render thread should have created the necessary resources before
  // signaling the main thread
  tb_reimport_swapchain(self->rt_sys);

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
    reimport_render_pass(self, self->brightness_pass);
    reimport_render_pass(self, self->bloom_blur_pass);
    reimport_render_pass(self, self->bloom_downsample_pass);
    reimport_render_pass(self, self->bloom_upsample_pass);
    reimport_render_pass(self, self->tonemap_pass);
    reimport_render_pass(self, self->fxaa_pass);
    reimport_render_pass(self, self->ui_pass);
  }

  // We now need to patch every pass's transitions so that their targets point
  // at the right VkImages
  TB_DYN_ARR_FOREACH(self->render_passes, pass_idx) {
    TbRenderPass *pass = &TB_DYN_ARR_AT(self->render_passes, pass_idx);
    for (uint32_t trans_idx = 0; trans_idx < pass->transition_count;
         ++trans_idx) {
      for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES;
           ++frame_idx) {
        TbFrameState *state =
            &self->rnd_sys->render_thread->frame_states[frame_idx];
        TbPassContext *context = &TB_DYN_ARR_AT(state->pass_contexts, pass_idx);
        const PassTransition *transition = &pass->transitions[trans_idx];
        TbImageTransition *barrier = &context->barriers[trans_idx];
        *barrier = transition->barrier;
        barrier->barrier.image = tb_render_target_get_image(
            self->rt_sys, frame_idx, transition->render_target);
      }
    }
  }

  // Also clear out any draws that were in flight on the render thread
  // Any draws that had descriptors that point to these re-created resources
  // are invalid
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    TbFrameState *state =
        &self->rnd_sys->render_thread->frame_states[frame_idx];
    TB_DYN_ARR_FOREACH(state->draw_contexts, ctx_idx) {
      TbDrawContext *draw_ctx = &TB_DYN_ARR_AT(state->draw_contexts, ctx_idx);
      draw_ctx->batch_count = 0;
    }
    TB_DYN_ARR_FOREACH(state->dispatch_contexts, ctx_idx) {
      TbDispatchContext *dispatch_ctx =
          &TB_DYN_ARR_AT(state->dispatch_contexts, ctx_idx);
      dispatch_ctx->batch_count = 0;
    }
  }
}

TbDrawContextId
tb_render_pipeline_register_draw_context(TbRenderPipelineSystem *self,
                                         const TbDrawContextDescriptor *desc) {
  TbRenderThread *thread = self->rnd_sys->render_thread;
  TbDrawContextId id = TB_DYN_ARR_SIZE(thread->frame_states[0].draw_contexts);
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    TbFrameState *state = &thread->frame_states[frame_idx];
    TbDrawContext ctx = {
        .pass_id = desc->pass_id,
        .user_batch_size = desc->batch_size,
        .record_fn = desc->draw_fn,
    };
    TB_DYN_ARR_APPEND(state->draw_contexts, ctx);
  }
  return id;
}

TbDispatchContextId tb_render_pipeline_register_dispatch_context(
    TbRenderPipelineSystem *self, const TbDispatchContextDescriptor *desc) {
  TbRenderThread *thread = self->rnd_sys->render_thread;
  TbDispatchContextId id =
      TB_DYN_ARR_SIZE(thread->frame_states[0].dispatch_contexts);
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    TbFrameState *state = &thread->frame_states[frame_idx];
    TbDispatchContext ctx = {
        .pass_id = desc->pass_id,
        .user_batch_size = desc->batch_size,
        .record_fn = desc->dispatch_fn,
    };
    TB_DYN_ARR_APPEND(state->dispatch_contexts, ctx);
  }
  return id;
}

void tb_render_pipeline_get_attachments(TbRenderPipelineSystem *self,
                                        TbRenderPassId pass,
                                        uint32_t *attach_count,
                                        TbPassAttachment *attachments) {
  TB_CHECK(pass < TB_DYN_ARR_SIZE(self->render_passes), "Pass Id out of range");
  TB_CHECK(attach_count, "Attachment count pointer must be valid");
  TB_CHECK(*attach_count <= TB_MAX_ATTACHMENTS, "Too many attachments");

  const TbRenderPass *p = &TB_DYN_ARR_AT(self->render_passes, pass);

  if (attachments == NULL) {
    // Attachments ptr was not specified, set the attachment count and return
    *attach_count = p->attach_count;
    return;
  } else {
    // Attachment count and attachment pointers were provided
    TB_CHECK(*attach_count == p->attach_count, "Unexpected size mismatch");
    SDL_memcpy(attachments, p->attachments,
               sizeof(TbPassAttachment) * (*attach_count));
  }
}

void tb_render_pipeline_issue_draw_batch(TbRenderPipelineSystem *self,
                                         TbDrawContextId draw_ctx,
                                         uint32_t batch_count,
                                         const TbDrawBatch *batches) {
  TbRenderThread *thread = self->rnd_sys->render_thread;
  TbFrameState *state = &thread->frame_states[self->rnd_sys->frame_idx];
  if (draw_ctx >= TB_DYN_ARR_SIZE(state->draw_contexts)) {
    TB_CHECK(false, "Draw Context Id out of range");
    return;
  }

  TbDrawContext *ctx = &TB_DYN_ARR_AT(state->draw_contexts, draw_ctx);

  const uint32_t write_head = ctx->batch_count;
  const uint32_t new_count = ctx->batch_count + batch_count;
  if (new_count > ctx->batch_max) {
    const uint32_t new_max = new_count * 2;
    // We want to realloc the user batches first because their pointers
    // changing is what we have to fix up
    ctx->user_batches = tb_realloc(self->gp_alloc, ctx->user_batches,
                                   new_max * ctx->user_batch_size);
    ctx->batches =
        tb_realloc_nm_tp(self->gp_alloc, ctx->batches, new_max, TbDrawBatch);
    ctx->batch_max = new_max;

    // Pointer Fixup
    for (uint32_t i = 0; i < new_count; ++i) {
      TbDrawBatch *batch = &ctx->batches[i];
      batch->user_batch =
          (uint8_t *)ctx->user_batches + (ctx->user_batch_size * i);
    }
  }

  for (uint32_t i = 0; i < batch_count; ++i) {
    const TbDrawBatch *batch = &batches[i];
    void *user_dst = ((uint8_t *)ctx->user_batches) +
                     ((i + write_head) * ctx->user_batch_size);
    SDL_memcpy(user_dst, batch->user_batch, ctx->user_batch_size);
    TbDrawBatch *write_batch = &ctx->batches[i + write_head];

    // Must always copy draw data
    void *draws = write_batch->draws;
    if (batch->draw_count > write_batch->draw_max) {
      draws = tb_realloc(self->gp_alloc, draws,
                         batch->draw_count * batch->draw_size);
    }
    SDL_memcpy(draws, batch->draws, batch->draw_count * batch->draw_size);
    *write_batch = *batch;
    write_batch->user_batch = user_dst;
    write_batch->draws = draws;
    if (batch->draw_count > write_batch->draw_max) {
      write_batch->draw_max = batch->draw_count;
    }
  }

  ctx->batch_count = new_count;
}

void tb_render_pipeline_issue_dispatch_batch(TbRenderPipelineSystem *self,
                                             TbDispatchContextId dispatch_ctx,
                                             uint32_t batch_count,
                                             const TbDispatchBatch *batches) {
  TbRenderThread *thread = self->rnd_sys->render_thread;
  TbFrameState *state = &thread->frame_states[self->rnd_sys->frame_idx];
  if (dispatch_ctx >= TB_DYN_ARR_SIZE(state->dispatch_contexts)) {
    TB_CHECK(false, "Dispatch Context Id out of range");
    return;
  }

  TbDispatchContext *ctx =
      &TB_DYN_ARR_AT(state->dispatch_contexts, dispatch_ctx);

  const uint32_t write_head = ctx->batch_count;
  const uint32_t new_count = ctx->batch_count + batch_count;
  if (new_count > ctx->batch_max) {
    const uint32_t new_max = new_count * 2;
    // We want to realloc the user batches first because their pointers
    // changing is what we have to fix up
    ctx->user_batches = tb_realloc(self->gp_alloc, ctx->user_batches,
                                   new_max * ctx->user_batch_size);
    ctx->batches = tb_realloc_nm_tp(self->gp_alloc, ctx->batches, new_max,
                                    TbDispatchBatch);
    // Pointer Fixup
    for (uint32_t i = 0; i < batch_count; ++i) {
      ctx->batches[i].user_batch =
          (uint8_t *)ctx->user_batches + (ctx->user_batch_size * i);
    }

    ctx->batch_max = new_max;
  }

  // Copy batches into frame state's batch list
  TbDispatchBatch *dst = &ctx->batches[write_head];
  SDL_memcpy(dst, batches, batch_count * sizeof(TbDispatchBatch));

  for (uint32_t i = 0; i < 0 + batch_count; ++i) {
    void *user_dst = ((uint8_t *)ctx->user_batches) +
                     ((i + write_head) * ctx->user_batch_size);
    SDL_memcpy(user_dst, batches[i].user_batch, ctx->user_batch_size);
    ctx->batches[i + write_head].user_batch = user_dst;
  }

  ctx->batch_count = new_count;
}
