#include "tb_mesh_rnd_sys.h"

#include "cgltf.h"
#include "tb_camera_component.h"
#include "tb_common.slangh"
#include "tb_gltf.h"
#include "tb_gltf.slangh"
#include "tb_hash.h"
#include "tb_light_component.h"
#include "tb_material_system.h"
#include "tb_mesh_component.h"
#include "tb_mesh_system.h"
#include "tb_profiling.h"
#include "tb_render_object_system.h"
#include "tb_render_pipeline_system.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_shader_system.h"
#include "tb_texture_system.h"
#include "tb_transform_component.h"
#include "tb_util.h"
#include "tb_view_system.h"
#include "tb_vk_dbg.h"
#include "tb_world.h"

#include <flecs.h>

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "tb_gltf_frag.h"
#include "tb_gltf_two_frag.h"
#include "tb_gltf_two_mesh.h"
#include "tb_gltf_vert.h"
#include "tb_opaque_prepass_frag.h"
#include "tb_opaque_prepass_two_frag.h"
#include "tb_opaque_prepass_two_mesh.h"
#include "tb_opaque_prepass_vert.h"
#pragma clang diagnostic pop

ECS_COMPONENT_DECLARE(TbMeshSystem);
ECS_TAG_DECLARE(TbMeshInGPUScene);

typedef struct VkBufferView_T *VkBufferView;

typedef struct TbMesh {
  TbMeshId id;
  uint32_t ref_count;
  TbHostBuffer host_buffer;
  TbBuffer gpu_buffer;
  VkIndexType idx_type;
  VkBufferView index_view;
  VkBufferView attr_views[TB_INPUT_PERM_COUNT];
} TbMesh;

// Helper macro to auto-register system
TB_REGISTER_SYS(tb, mesh, TB_MESH_RND_SYS_PRIO)

typedef struct TbMeshShaderArgs {
  TbRenderSystem *rnd_sys;
  VkFormat depth_format;
  VkFormat color_format;
  VkPipelineLayout pipe_layout;
} TbMeshShaderArgs;

VkPipeline create_prepass_mesh_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Prepass Mesh Shader Pipeline");
  tb_auto pipe_args = (const TbMeshShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  VkShaderModule mesh_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(tb_opaque_prepass_two_mesh);
    create_info.pCode = (const uint32_t *)tb_opaque_prepass_two_mesh;
    tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Mesh",
                         &mesh_mod);

    create_info.codeSize = sizeof(tb_opaque_prepass_two_frag);
    create_info.pCode = (const uint32_t *)tb_opaque_prepass_two_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Frag2",
                         &frag_mod);
  }

  VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
                  .module = mesh_mod,
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
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
              .cullMode = VK_CULL_MODE_NONE, // We cull in the shader
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
              .pAttachments = (VkPipelineColorBlendAttachmentState[1]){{
                  .blendEnable = VK_FALSE,
                  .colorWriteMask =
                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
              }},
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS,
#endif
              .maxDepthBounds = 1.0f,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates =
                  (VkDynamicState[2]){
                      VK_DYNAMIC_STATE_VIEWPORT,
                      VK_DYNAMIC_STATE_SCISSOR,
                  },
          },
      .layout = pipe_layout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Opaque Prepass Mesh Shader Pipeline",
                                   &pipeline);

  tb_rnd_destroy_shader(rnd_sys, mesh_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_prepass_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Prepass Pipeline");
  tb_auto pipe_args = (const TbMeshShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(tb_opaque_prepass_vert);
    create_info.pCode = (const uint32_t *)tb_opaque_prepass_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Vert",
                         &vert_mod);

    create_info.codeSize = sizeof(tb_opaque_prepass_frag);
    create_info.pCode = (const uint32_t *)tb_opaque_prepass_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Frag",
                         &frag_mod);
  }

  VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
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
              .cullMode = VK_CULL_MODE_NONE, // We cull in the shader
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
              .pAttachments = (VkPipelineColorBlendAttachmentState[1]){{
                  .blendEnable = VK_FALSE,
                  .colorWriteMask =
                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
              }},
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS,
#endif
              .maxDepthBounds = 1.0f,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates =
                  (VkDynamicState[2]){
                      VK_DYNAMIC_STATE_VIEWPORT,
                      VK_DYNAMIC_STATE_SCISSOR,
                  },
          },
      .layout = pipe_layout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Opaque Prepass Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_opaque_mesh_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Opaque Mesh Pipeline");
  tb_auto pipe_args = (const TbMeshShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto color_format = pipe_args->color_format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  // Load Shader Modules
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_vert),
        .pCode = (const uint32_t *)tb_gltf_vert,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Vert", &vert_mod);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_frag),
        .pCode = (const uint32_t *)tb_gltf_frag,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag", &frag_mod);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "main",
              }},
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
              .pViewports = (VkViewport[1]){{0, 600.0f, 800.0f, -600.0f, 0, 1}},
              .scissorCount = 1,
              .pScissors = (VkRect2D[1]){{{0, 0}, {800, 600}}},
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
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
#endif
              .maxDepthBounds = 1.0f,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments =
                  (VkPipelineColorBlendAttachmentState[1]){
                      {
                          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT,
                      },
                  },
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

  // Create pipeline
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Opaque Mesh Pipeline", &pipeline);

  // Can destroy shader moduless
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_transparent_mesh_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Transparent Mesh Pipeline");
  tb_auto pipe_args = (const TbMeshShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto color_format = pipe_args->color_format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  // Load Shader Modules
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_vert),
        .pCode = (const uint32_t *)tb_gltf_vert,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Vert", &vert_mod);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_frag),
        .pCode = (const uint32_t *)tb_gltf_frag,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag", &frag_mod);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "main",
              }},
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
              .pViewports = (VkViewport[1]){{0, 600.0f, 800.0f, -600.0f, 0, 1}},
              .scissorCount = 1,
              .pScissors = (VkRect2D[1]){{{0, 0}, {800, 600}}},
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
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
#endif
              .maxDepthBounds = 1.0f,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments = (VkPipelineColorBlendAttachmentState[1]){{
                  .blendEnable = VK_TRUE,
                  .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                  .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                  .colorBlendOp = VK_BLEND_OP_ADD,
                  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                  .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                  .alphaBlendOp = VK_BLEND_OP_ADD,
                  .colorWriteMask =
                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
              }}},
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = pipe_layout,
  };

  // Create pipeline
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Transparent Mesh Pipeline", &pipeline);

  // Can destroy shader moduless
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

// For next gen mesh shaders
VkPipeline create_opaque_mesh_pipeline2(void *args) {
  TB_TRACY_SCOPE("Create Opaque Mesh Shader Pipeline");
  tb_auto pipe_args = (const TbMeshShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto color_format = pipe_args->color_format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  // Load Shader Modules
  VkShaderModule mesh_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_two_mesh),
        .pCode = (const uint32_t *)tb_gltf_two_mesh,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Mesh", &mesh_mod);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_two_frag),
        .pCode = (const uint32_t *)tb_gltf_two_frag,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag2", &frag_mod);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
                  .module = mesh_mod,
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "main",
              }},
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
              .pViewports = (VkViewport[1]){{0, 600.0f, 800.0f, -600.0f, 0, 1}},
              .scissorCount = 1,
              .pScissors = (VkRect2D[1]){{{0, 0}, {800, 600}}},
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
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
#endif
              .maxDepthBounds = 1.0f,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments =
                  (VkPipelineColorBlendAttachmentState[1]){
                      {
                          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT,
                      },
                  },
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

  // Create pipeline
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Opaque Mesh Shader Pipeline", &pipeline);

  // Can destroy shader moduless
  tb_rnd_destroy_shader(rnd_sys, mesh_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_transparent_mesh_pipeline2(void *args) {
  TB_TRACY_SCOPE("Create Transparent Mesh Shader Pipeline");
  tb_auto pipe_args = (const TbMeshShaderArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto color_format = pipe_args->color_format;
  tb_auto pipe_layout = pipe_args->pipe_layout;

  // Load Shader Modules
  VkShaderModule mesh_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_two_mesh),
        .pCode = (const uint32_t *)tb_gltf_two_mesh,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Mesh", &mesh_mod);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(tb_gltf_two_frag),
        .pCode = (const uint32_t *)tb_gltf_two_frag,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag2", &frag_mod);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
                  .module = mesh_mod,
                  .pName = "main",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "main",
              }},
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
              .pViewports = (VkViewport[1]){{0, 600.0f, 800.0f, -600.0f, 0, 1}},
              .scissorCount = 1,
              .pScissors = (VkRect2D[1]){{{0, 0}, {800, 600}}},
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
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
#endif
              .maxDepthBounds = 1.0f,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
              .attachmentCount = 1,
              .pAttachments = (VkPipelineColorBlendAttachmentState[1]){{
                  .blendEnable = VK_TRUE,
                  .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                  .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                  .colorBlendOp = VK_BLEND_OP_ADD,
                  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                  .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                  .alphaBlendOp = VK_BLEND_OP_ADD,
                  .colorWriteMask =
                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
              }}},
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = pipe_layout,
  };

  // Create pipeline
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(
      rnd_sys, 1, &create_info, "Transparent Mesh Shader Pipeline", &pipeline);

  // Can destroy shader moduless
  tb_rnd_destroy_shader(rnd_sys, mesh_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

void prepass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                    uint32_t batch_count, const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Opaque Prepass", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Prepass", 3, true);
  cmd_begin_label(buffer, "Opaque Prepass", (float4){0.0f, 0.0f, 1.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const TbPrimitiveBatch *prim_batch =
        (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TB_TRACY_SCOPEC("Record Mesh", TracyCategoryColorRendering);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 6;

#if TB_USE_DESC_BUFFER == 1
    {
      const VkDescriptorBufferBindingInfoEXT buffer_bindings[set_count] = {
          prim_batch->view_addr, prim_batch->draw_addr, prim_batch->obj_addr,
          prim_batch->idx_addr,  prim_batch->pos_addr,  prim_batch->norm_addr,
      };
      vkCmdBindDescriptorBuffersEXT(buffer, set_count, buffer_bindings);
      uint32_t buf_indices[set_count] = {0, 1, 2, 3, 4, 5};
      VkDeviceSize buf_offsets[set_count] = {0};
      vkCmdSetDescriptorBufferOffsetsEXT(
          buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, set_count,
          buf_indices, buf_offsets);
    }
#else
    {
      VkDescriptorSet sets[set_count] = {
          prim_batch->view_set, prim_batch->draw_set, prim_batch->obj_set,
          prim_batch->idx_set,  prim_batch->pos_set,  prim_batch->norm_set};
      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              0, set_count, sets, 0, NULL);
    }
#endif

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      TB_TRACY_SCOPEC("Record Indirect Draw", TracyCategoryColorRendering);
      vkCmdDrawIndirect(buffer, draw->buffer, draw->offset, draw->draw_count,
                        draw->stride);
    }

    cmd_end_label(buffer);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

void prepass_meshlet_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                            uint32_t batch_count, const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Opaque Meshlet Prepass", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Prepass", 3, true);
  cmd_begin_label(buffer, "Opaque Prepass", (float4){0.0f, 0.0f, 1.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const TbPrimitiveBatch *prim_batch =
        (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TB_TRACY_SCOPEC("Record Mesh", TracyCategoryColorRendering);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 9;

    VkDescriptorSet sets[set_count] = {
        prim_batch->view_set, prim_batch->draw_set, prim_batch->meshlet_set,
        prim_batch->tri_set,  prim_batch->vert_set, prim_batch->obj_set,
        prim_batch->idx_set,  prim_batch->pos_set,  prim_batch->norm_set};
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            set_count, sets, 0, NULL);

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      if (draw->draw_count == 0) {
        continue;
      }
      TB_TRACY_SCOPEC("Record Indirect Draw", TracyCategoryColorRendering);
      vkCmdDrawMeshTasksIndirectEXT(buffer, draw->buffer, draw->offset,
                                    draw->draw_count, draw->stride);
    }

    cmd_end_label(buffer);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

void mesh_record_common(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  (void)gpu_ctx;
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const TbPrimitiveBatch *prim_batch =
        (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TB_TRACY_SCOPEC("Record Mesh", TracyCategoryColorRendering);
    cmd_begin_label(buffer, "Mesh Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 10;

#if TB_USE_DESC_BUFFER == 1
    {
      const VkDescriptorBufferBindingInfoEXT buffer_bindings[set_count] = {
          prim_batch->view_addr, prim_batch->mat_addr,  prim_batch->draw_addr,
          prim_batch->obj_addr,  prim_batch->tex_addr,  prim_batch->idx_addr,
          prim_batch->pos_addr,  prim_batch->norm_addr, prim_batch->tan_addr,
          prim_batch->uv0_addr,
      };
      vkCmdBindDescriptorBuffersEXT(buffer, set_count, buffer_bindings);
      uint32_t buf_indices[set_count] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
      VkDeviceSize buf_offsets[set_count] = {0};
      vkCmdSetDescriptorBufferOffsetsEXT(
          buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, set_count,
          buf_indices, buf_offsets);
    }
#else
    {
      const VkDescriptorSet sets[set_count] = {
          prim_batch->view_set, prim_batch->mat_set,  prim_batch->draw_set,
          prim_batch->obj_set,  prim_batch->tex_set,  prim_batch->idx_set,
          prim_batch->pos_set,  prim_batch->norm_set, prim_batch->tan_set,
          prim_batch->uv0_set,
      };
      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              0, set_count, sets, 0, NULL);
    }
#endif

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      TB_TRACY_SCOPEC("Record Indirect Draw", TracyCategoryColorRendering);
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      vkCmdDrawIndirect(buffer, draw->buffer, draw->offset, draw->draw_count,
                        draw->stride);
    }

    cmd_end_label(buffer);
  }
}

void meshlet_record_common(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                           uint32_t batch_count, const TbDrawBatch *batches) {
  (void)gpu_ctx;
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const TbPrimitiveBatch *prim_batch =
        (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TB_TRACY_SCOPEC("Record Mesh", TracyCategoryColorRendering);
    cmd_begin_label(buffer, "Mesh Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 13;

    const VkDescriptorSet sets[set_count] = {
        prim_batch->view_set,    prim_batch->mat_set,  prim_batch->draw_set,
        prim_batch->meshlet_set, prim_batch->tri_set,  prim_batch->vert_set,
        prim_batch->obj_set,     prim_batch->tex_set,  prim_batch->idx_set,
        prim_batch->pos_set,     prim_batch->norm_set, prim_batch->tan_set,
        prim_batch->uv0_set,
    };
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            set_count, sets, 0, NULL);

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      TB_TRACY_SCOPEC("Record Indirect Draw", TracyCategoryColorRendering);
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      if (draw->draw_count == 0) {
        continue;
      }
      vkCmdDrawMeshTasksIndirectEXT(buffer, draw->buffer, draw->offset,
                                    draw->draw_count, draw->stride);
    }

    cmd_end_label(buffer);
  }
}

void opaque_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Opaque Mesh Record", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Meshes", 3, true);
  cmd_begin_label(buffer, "Opaque Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

void transparent_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                             uint32_t batch_count, const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Transparent Mesh Record", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Transparent Meshes", 3,
                    true);
  cmd_begin_label(buffer, "Transparent Meshes",
                  (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

void opaque_meshlet_pass_record(TracyCGPUContext *gpu_ctx,
                                VkCommandBuffer buffer, uint32_t batch_count,
                                const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Opaque Meshlet Record", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Meshes", 3, true);
  cmd_begin_label(buffer, "Opaque Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});
  meshlet_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

void transparent_meshlet_pass_record(TracyCGPUContext *gpu_ctx,
                                     VkCommandBuffer buffer,
                                     uint32_t batch_count,
                                     const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Transparent Meshlet Record", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Transparent Meshes", 3,
                    true);
  cmd_begin_label(buffer, "Transparent Meshes",
                  (float4){0.0f, 0.0f, 1.0f, 1.0f});
  meshlet_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

TbMeshSystem create_mesh_system_internal(ecs_world_t *ecs, TbAllocator gp_alloc,
                                         TbAllocator tmp_alloc,
                                         TbRenderSystem *rnd_sys,
                                         TbViewSystem *view_sys,
                                         TbRenderPipelineSystem *rp_sys) {
  TbMeshSystem sys = {
      .gp_alloc = gp_alloc,
      .tmp_alloc = tmp_alloc,
      .rnd_sys = rnd_sys,
      .view_sys = view_sys,
      .rp_sys = rp_sys,
  };
  TB_DYN_ARR_RESET(sys.indirect_opaque_draws, gp_alloc, 8);
  TB_DYN_ARR_RESET(sys.indirect_trans_draws, gp_alloc, 8);
  TB_DYN_ARR_RESET(sys.opaque_draw_data, gp_alloc, 8);
  TB_DYN_ARR_RESET(sys.trans_draw_data, gp_alloc, 8);
  TbRenderPassId prepass_id = rp_sys->opaque_depth_normal_pass;
  TbRenderPassId opaque_pass_id = rp_sys->opaque_color_pass;
  TbRenderPassId transparent_pass_id = rp_sys->transparent_color_pass;

  tb_auto meshlet_set_layout = tb_mesh_sys_get_meshlet_set_layout(ecs);
  tb_auto mesh_set_layout = tb_mesh_sys_get_attr_set_layout(ecs);

  // Setup mesh system for rendering
  {
    VkResult err = VK_SUCCESS;

    // Create instance descriptor set layout
    {
      VkDescriptorSetLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 1,
#if TB_USE_DESC_BUFFER == 1
          .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
          .pBindings =
              (VkDescriptorSetLayoutBinding[1]){
                  {
                      .binding = 0,
                      .descriptorCount = 1,
                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                    VK_SHADER_STAGE_MESH_BIT_EXT |
                                    VK_SHADER_STAGE_FRAGMENT_BIT,
                  },
              },
      };
      err = tb_rnd_create_set_layout(rnd_sys, &create_info, "Instance Layout",
                                     &sys.draw_set_layout);
      TB_VK_CHECK(err, "Failed to create instanced set layout");
    }

    // Create prepass pipeline layout
    {
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 6,
          .pSetLayouts =
              (VkDescriptorSetLayout[6]){
                  tb_view_sys_get_set_layout(ecs),
                  sys.draw_set_layout,
                  tb_render_object_sys_get_set_layout(ecs),
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
              },
      };
      err = tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                          "Opaque Depth Normal Prepass Layout",
                                          &sys.prepass_layout);
      TB_VK_CHECK(err, "Failed to create opaque prepass set layout");
    }

    // Create prepass pipeline
    {
      TbMeshShaderArgs args = {
          .rnd_sys = rnd_sys,
          .depth_format = VK_FORMAT_D32_SFLOAT,
          .pipe_layout = sys.prepass_layout,
      };
      sys.prepass_shader = tb_shader_load(ecs, create_prepass_pipeline, &args,
                                          sizeof(TbMeshShaderArgs));
    }

    // Create prepass mesh shaders pipeline layout
    {
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 9,
          .pSetLayouts =
              (VkDescriptorSetLayout[9]){
                  tb_view_sys_get_set_layout(ecs),
                  sys.draw_set_layout,
                  meshlet_set_layout,
                  meshlet_set_layout,
                  meshlet_set_layout,
                  tb_render_object_sys_get_set_layout(ecs),
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
              },
      };
      err = tb_rnd_create_pipeline_layout(
          rnd_sys, &create_info,
          "Opaque Depth Normal Prepass Mesh Shader Layout",
          &sys.prepass_mesh_layout);
      TB_VK_CHECK(err,
                  "Failed to create opaque prepass mesh shader set layout");
    }

    // Create prepass mesh shader pipeline
    {
      TbMeshShaderArgs args = {
          .rnd_sys = rnd_sys,
          .depth_format = VK_FORMAT_D32_SFLOAT,
          .pipe_layout = sys.prepass_mesh_layout,
      };
      sys.prepass_mesh_shader = tb_shader_load(
          ecs, create_prepass_mesh_pipeline, &args, sizeof(TbMeshShaderArgs));
    }

    // Create pipeline layouts
    {
      const uint32_t layout_count = 12;
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = layout_count,
          .pSetLayouts =
              (VkDescriptorSetLayout[layout_count]){
                  tb_view_sys_get_set_layout(ecs),
                  tb_mat_sys_get_set_layout(ecs),
                  sys.draw_set_layout,
                  tb_render_object_sys_get_set_layout(ecs),
                  tb_tex_sys_get_set_layout(ecs),
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
              },
      };
      tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                    "GLTF Pipeline Layout", &sys.pipe_layout);
    }

    // Create mesh shader pipeline layouts
    {
      const uint32_t layout_count = 15;
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = layout_count,
          .pSetLayouts =
              (VkDescriptorSetLayout[layout_count]){
                  tb_view_sys_get_set_layout(ecs),
                  tb_mat_sys_get_set_layout(ecs),
                  sys.draw_set_layout,
                  meshlet_set_layout,
                  meshlet_set_layout,
                  meshlet_set_layout,
                  tb_render_object_sys_get_set_layout(ecs),
                  tb_tex_sys_get_set_layout(ecs),
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
                  mesh_set_layout,
              },
      };
      tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                    "GLTF Mesh Shader Pipeline Layout",
                                    &sys.mesh_pipe_layout);
    }

    // Create opaque and transparent pipelines
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(sys.rp_sys,
                                         sys.rp_sys->opaque_depth_normal_pass,
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 2, "Unexpected");
      TbPassAttachment depth_info = {0};
      tb_render_pipeline_get_attachments(sys.rp_sys,
                                         sys.rp_sys->opaque_depth_normal_pass,
                                         &attach_count, &depth_info);

      TbMeshShaderArgs args = {
          .rnd_sys = rnd_sys,
          .depth_format = tb_render_target_get_format(sys.rp_sys->rt_sys,
                                                      depth_info.attachment),
          .pipe_layout = sys.pipe_layout,
      };

      tb_render_pipeline_get_attachments(
          sys.rp_sys, sys.rp_sys->opaque_color_pass, &attach_count, NULL);
      TB_CHECK(attach_count == 2, "Unexpected");
      TbPassAttachment attach_info[2] = {0};
      tb_render_pipeline_get_attachments(sys.rp_sys,
                                         sys.rp_sys->opaque_color_pass,
                                         &attach_count, attach_info);

      for (uint32_t i = 0; i < attach_count; i++) {
        VkFormat format = tb_render_target_get_format(
            sys.rp_sys->rt_sys, attach_info[i].attachment);
        if (format != VK_FORMAT_D32_SFLOAT) {
          args.color_format = format;
          break;
        }
      }

      sys.opaque_shader = tb_shader_load(ecs, create_opaque_mesh_pipeline,
                                         &args, sizeof(TbMeshShaderArgs));
      sys.transparent_shader =
          tb_shader_load(ecs, create_transparent_mesh_pipeline, &args,
                         sizeof(TbMeshShaderArgs));

      TbMeshShaderArgs mesh_args = args;
      mesh_args.pipe_layout = sys.mesh_pipe_layout;

      sys.opaque_mesh_shader =
          tb_shader_load(ecs, create_opaque_mesh_pipeline2, &mesh_args,
                         sizeof(TbMeshShaderArgs));
      sys.transparent_mesh_shader =
          tb_shader_load(ecs, create_transparent_mesh_pipeline2, &mesh_args,
                         sizeof(TbMeshShaderArgs));
    }
  }

#if TB_USE_DESC_BUFFER == 1
  // Create descriptor buffers for opaque and transparent draws
  tb_create_descriptor_buffer(rnd_sys, sys.draw_set_layout,
                              "Opaque Draw Desc Buffer", 1,
                              &sys.opaque_draw_descs);
  tb_create_descriptor_buffer(rnd_sys, sys.draw_set_layout,
                              "Transparent Draw Desc Buffer", 1,
                              &sys.trans_draw_descs);
#endif

  // Register drawing with the pipelines
  sys.prepass_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(TbPrimitiveBatch),
                  .draw_fn = prepass_meshlet_record,
                  .pass_id = prepass_id,
              });
  sys.opaque_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(TbPrimitiveBatch),
                  .draw_fn = opaque_meshlet_pass_record,
                  .pass_id = opaque_pass_id,
              });
  sys.transparent_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(TbPrimitiveBatch),
                  .draw_fn = transparent_meshlet_pass_record,
                  .pass_id = transparent_pass_id,
              });
  return sys;
}

void destroy_mesh_system(ecs_world_t *ecs, TbMeshSystem *self) {
  TbRenderSystem *rnd_sys = self->rnd_sys;

  tb_shader_destroy(ecs, self->opaque_shader);
  tb_shader_destroy(ecs, self->transparent_shader);
  tb_shader_destroy(ecs, self->prepass_shader);
  tb_shader_destroy(ecs, self->prepass_mesh_shader);

  tb_rnd_destroy_pipe_layout(rnd_sys, self->pipe_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->prepass_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->prepass_mesh_layout);

  tb_destroy_descriptor_buffer(rnd_sys, &self->opaque_draw_descs);
  tb_destroy_descriptor_buffer(rnd_sys, &self->trans_draw_descs);

  *self = (TbMeshSystem){0};
}

void mesh_sort_new(ecs_iter_t *it) {
  TB_TRACY_SCOPEC("Mesh Sort New", TracyCategoryColorRendering);
  ecs_world_t *ecs = it->world;

  tb_auto mesh_sys = ecs_field(it, TbMeshSystem, 0);
  tb_auto mesh_comps = ecs_field(it, TbMeshComponent, 1);
  tb_auto render_objects = ecs_field(it, TbRenderObject, 2);

  for (int32_t i = 0; i < it->count; ++i) {
    TbMesh2 mesh = mesh_comps[i].mesh2;
    tb_auto render_object = render_objects[i].index;

    if (!tb_is_mesh_ready(ecs, mesh)) {
      continue;
    }

    if (ecs_has(ecs, mesh, TbMeshInGPUScene)) {
      // SDL_assert(false);
      continue;
    }

    tb_auto mesh_desc_idx = *ecs_get(ecs, mesh, TbMeshIndex);

    // Don't make any mutations until we can guarantee that the mesh is ready
    bool all_children_ready = true;
    tb_auto submesh_itr = ecs_children(ecs, mesh);
    while (ecs_children_next(&submesh_itr)) {
      for (int32_t sm_i = 0; sm_i < submesh_itr.count; ++sm_i) {
        TbSubMesh2 sm_ent = submesh_itr.entities[sm_i];
        if (!ecs_has(ecs, sm_ent, TbSubMesh2Data)) {
          TB_CHECK(false, "Submesh entity unexpectedly lacked submesh data");
          continue;
        }
        tb_auto sm = ecs_get(ecs, sm_ent, TbSubMesh2Data);
        // Material should be loaded and ready if mesh was reported as ready
        if (!tb_is_material_ready(ecs, sm->material)) {
          all_children_ready = false;
          break;
        }
      }
      if (all_children_ready == false) {
        break;
      }
    }

    if (all_children_ready == false) {
      continue;
    }

    // All submesh materials are guaranteed ready
    submesh_itr = ecs_children(ecs, mesh);
    while (ecs_children_next(&submesh_itr)) {
      for (int32_t sm_i = 0; sm_i < submesh_itr.count; ++sm_i) {
        TbSubMesh2 sm_ent = submesh_itr.entities[sm_i];
        if (!ecs_has(ecs, sm_ent, TbSubMesh2Data)) {
          TB_CHECK(false, "Submesh entity unexpectedly lacked submesh data");
          continue;
        }
        tb_auto sm = ecs_get(ecs, sm_ent, TbSubMesh2Data);

        VkDrawMeshTasksIndirectCommandEXT indirect_draw = {
            .groupCountX = sm->meshlet_count,
            .groupCountY = 1,
            .groupCountZ = 1,
        };
        TbGLTFDrawData draw_data = {
            .perm = sm->vertex_perm,
            .obj_idx = render_object,
            .mesh_idx = mesh_desc_idx,
            .mat_idx = *ecs_get(ecs, sm->material, TbMaterialComponent),
            .index_offset = sm->index_offset,
            .meshlet_offset = sm->meshlet_offset,
            .vertex_offset = sm->vertex_offset,
        };

        if (tb_is_mat_transparent(ecs, sm->material)) {
          TB_DYN_ARR_APPEND(mesh_sys->indirect_trans_draws, indirect_draw);
          TB_DYN_ARR_APPEND(mesh_sys->trans_draw_data, draw_data);
        } else {
          TB_DYN_ARR_APPEND(mesh_sys->indirect_opaque_draws, indirect_draw);
          TB_DYN_ARR_APPEND(mesh_sys->opaque_draw_data, draw_data);
        }
      }
    }

    ecs_add(ecs, mesh, TbMeshInGPUScene);
  }
}

void mesh_construct_draws(ecs_iter_t *it) {
  TB_TRACY_SCOPEC("Mesh Construct Draws", TracyCategoryColorRendering);

  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 0);
  tb_auto mesh_sys = ecs_field(it, TbMeshSystem, 1);

  const uint64_t opaque_draw_count =
      TB_DYN_ARR_SIZE(mesh_sys->indirect_opaque_draws);
  TB_CHECK(opaque_draw_count == TB_DYN_ARR_SIZE(mesh_sys->opaque_draw_data),
           "Expected these arrays to be the same size");
  const uint64_t trans_draw_count =
      TB_DYN_ARR_SIZE(mesh_sys->indirect_trans_draws);
  TB_CHECK(trans_draw_count == TB_DYN_ARR_SIZE(mesh_sys->trans_draw_data),
           "Expected these arrays to be the same size");

  // Allocate indirect draw buffers
  VkDrawMeshTasksIndirectCommandEXT *opaque_draw_cmds = NULL;
  uint64_t opaque_cmds_offset = 0;
  {
    uint64_t size =
        sizeof(VkDrawMeshTasksIndirectCommandEXT) * opaque_draw_count;
    tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, size, 0x40, &opaque_cmds_offset,
                                   (void **)&opaque_draw_cmds);
  }
  VkDrawMeshTasksIndirectCommandEXT *trans_draw_cmds = NULL;
  uint64_t trans_cmds_offset = 0;
  {
    uint64_t size =
        sizeof(VkDrawMeshTasksIndirectCommandEXT) * trans_draw_count;
    tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, size, 0x40, &trans_cmds_offset,
                                   (void **)&trans_draw_cmds);
  }

  // Allocate per-draw storage buffers
  TbGLTFDrawData *opaque_draw_data = NULL;
  const uint64_t opaque_data_size = sizeof(TbGLTFDrawData) * opaque_draw_count;
  uint64_t opaque_data_offset = 0;
  tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, opaque_data_size, 0x40,
                                 &opaque_data_offset,
                                 (void **)&opaque_draw_data);

  TbGLTFDrawData *trans_draw_data = NULL;
  const uint64_t trans_data_size = sizeof(TbGLTFDrawData) * trans_draw_count;
  uint64_t trans_data_offset = 0;
  tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, trans_data_size, 0x40,
                                 &trans_data_offset, (void **)&trans_draw_data);

  // Write cmd buffers
  SDL_memcpy(opaque_draw_cmds, mesh_sys->indirect_opaque_draws.data,
             sizeof(VkDrawMeshTasksIndirectCommandEXT) * opaque_draw_count);
  SDL_memcpy(trans_draw_cmds, mesh_sys->indirect_trans_draws.data,
             sizeof(VkDrawMeshTasksIndirectCommandEXT) * trans_draw_count);

  // Write draw data buffers
  SDL_memcpy(opaque_draw_data, mesh_sys->opaque_draw_data.data,
             opaque_data_size);
  SDL_memcpy(trans_draw_data, mesh_sys->trans_draw_data.data, trans_data_size);

  mesh_sys->opaque_draw = (TbIndirectDraw){
      .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
      .offset = opaque_cmds_offset,
      .draw_count = opaque_draw_count,
      .stride = sizeof(VkDrawMeshTasksIndirectCommandEXT),
  };
  mesh_sys->trans_draw = (TbIndirectDraw){
      .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
      .offset = trans_cmds_offset,
      .draw_count = trans_draw_count,
      .stride = sizeof(VkDrawMeshTasksIndirectCommandEXT),
  };

  // Update descriptor sets
  {
#if TB_USE_DESC_BUFFER == 1
    // Reset descriptor buffers
    tb_reset_descriptor_buffer(rnd_sys, &mesh_sys->opaque_draw_descs);
    tb_reset_descriptor_buffer(rnd_sys, &mesh_sys->trans_draw_descs);
#else
    // Allocate per-draw descriptor sets
    const uint32_t set_count = 2;
    {
      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = set_count * 8,
          .poolSizeCount = 1,
          .pPoolSizes =
              (VkDescriptorPoolSize[1]){
                  {
                      .descriptorCount = set_count * 8,
                      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                  },
              },
      };
      VkDescriptorSetLayout layouts[set_count] = {
          mesh_sys->draw_set_layout,
          mesh_sys->draw_set_layout,
      };
      tb_rnd_frame_desc_pool_tick(rnd_sys, "mesh_draw_instances", &create_info,
                                  layouts, NULL, mesh_sys->draw_pools.pools,
                                  set_count, set_count);
    }
#endif

#if TB_USE_DESC_BUFFER == 1
    VkDescriptorBufferBindingInfoEXT opaque_draw_addr =
        tb_desc_buff_get_binding(&mesh_sys->opaque_draw_descs);
    VkDescriptorBufferBindingInfoEXT trans_draw_addr =
        tb_desc_buff_get_binding(&mesh_sys->trans_draw_descs);
#else
    VkDescriptorSet opaque_draw_set =
        tb_rnd_frame_desc_pool_get_set(rnd_sys, mesh_sys->draw_pools.pools, 0);
    VkDescriptorSet trans_draw_set =
        tb_rnd_frame_desc_pool_get_set(rnd_sys, mesh_sys->draw_pools.pools, 1);
#endif

#if TB_USE_DESC_BUFFER == 1
    {
      VkDeviceAddress tmp_buf_addr = tb_rnd_get_gpu_tmp_addr(rnd_sys);
      if (opaque_data_size > 0) {
        TbDescriptor desc = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .data.pStorageBuffer =
                &(VkDescriptorAddressInfoEXT){
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                    .address = tmp_buf_addr + opaque_data_offset,
                    .range = opaque_data_size,
                },
        };
        tb_write_desc_to_buffer(rnd_sys, &mesh_sys->opaque_draw_descs, 0,
                                &desc);
      }
      if (trans_data_size > 0) {
        TbDescriptor desc = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .data.pStorageBuffer =
                &(VkDescriptorAddressInfoEXT){
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                    .address = tmp_buf_addr + trans_data_offset,
                    .range = trans_data_size,
                },
        };
        tb_write_desc_to_buffer(rnd_sys, &mesh_sys->trans_draw_descs, 0, &desc);
      }
    }
#else
    // Write draw data buffer to descriptor sets
    {
      if (opaque_data_size > 0) {
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = opaque_draw_set,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
                    .offset = opaque_data_offset,
                    .range = opaque_data_size,
                },
        };
        tb_rnd_update_descriptors(rnd_sys, 1, &write);
      }

      if (trans_data_size > 0) {
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = trans_draw_set,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
                    .offset = trans_data_offset,
                    .range = trans_data_size,
                },
        };
        tb_rnd_update_descriptors(rnd_sys, 1, &write);
      }
    }
#endif
  }
}

void mesh_draw_tick(ecs_iter_t *it) {
  TB_TRACY_SCOPEC("Mesh Draw Tick", TracyCategoryColorRendering);
  ecs_world_t *ecs = it->world;

  tb_auto mesh_sys = ecs_field(it, TbMeshSystem, 0);
  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);
  tb_auto rp_sys = ecs_singleton_ensure(ecs, TbRenderPipelineSystem);
  tb_auto view_sys = ecs_singleton_ensure(ecs, TbViewSystem);

  // If any shaders aren't ready just bail
  if (!tb_is_shader_ready(ecs, mesh_sys->opaque_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->transparent_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->prepass_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->prepass_mesh_shader)) {
    return;
  }

  // For each camera
  tb_auto camera_it = ecs_query_iter(ecs, mesh_sys->camera_query);
  while (ecs_query_next(&camera_it)) {
    tb_auto cameras = ecs_field(&camera_it, TbCameraComponent, 0);
    for (int32_t cam_idx = 0; cam_idx < camera_it.count; ++cam_idx) {
      TB_TRACY_SCOPE("Camera");
      tb_auto camera = &cameras[cam_idx];
      tb_auto view_id = camera->view_id;
#if TB_USE_DESC_BUFFER == 1
      tb_auto view_addr = tb_view_sys_get_table_addr(ecs, view_id);
      // Skip camera if view set isn't ready
      if (view_addr.address == VK_NULL_HANDLE) {
        continue;
      }
#else
      tb_auto view_set = tb_view_system_get_descriptor(view_sys, view_id);
      // Skip camera if view set isn't ready
      if (view_set == VK_NULL_HANDLE) {
        continue;
      }
#endif

      const float width = camera->width;
      const float height = camera->height;

#if TB_USE_DESC_BUFFER == 1
      tb_auto obj_addr = tb_render_object_sys_get_table_addr(ecs);
      tb_auto tex_addr = tb_tex_sys_get_table_addr(ecs);
      tb_auto mat_addr = tb_mat_sys_get_table_addr(ecs);
      tb_auto idx_addr = tb_mesh_sys_get_idx_addr(ecs);
      tb_auto pos_addr = tb_mesh_sys_get_pos_addr(ecs);
      tb_auto norm_addr = tb_mesh_sys_get_norm_addr(ecs);
      tb_auto tan_addr = tb_mesh_sys_get_tan_addr(ecs);
      tb_auto uv0_addr = tb_mesh_sys_get_uv0_addr(ecs);
#else
      tb_auto obj_set = tb_render_object_sys_get_set(ecs);
      tb_auto tex_set = tb_tex_sys_get_set(ecs);
      tb_auto mat_set = tb_mat_sys_get_set(ecs);
      tb_auto idx_set = tb_mesh_sys_get_idx_set(ecs);
      tb_auto meshlet_set = tb_mesh_sys_get_meshlet_set(ecs);
      tb_auto tri_set = tb_mesh_sys_get_triangles_set(ecs);
      tb_auto vert_set = tb_mesh_sys_get_vertices_set(ecs);
      tb_auto pos_set = tb_mesh_sys_get_pos_set(ecs);
      tb_auto norm_set = tb_mesh_sys_get_norm_set(ecs);
      tb_auto tan_set = tb_mesh_sys_get_tan_set(ecs);
      tb_auto uv0_set = tb_mesh_sys_get_uv0_set(ecs);
#endif

#if TB_USE_DESC_BUFFER == 1
      VkDescriptorBufferBindingInfoEXT opaque_draw_addr =
          tb_desc_buff_get_binding(&mesh_sys->opaque_draw_descs);
      VkDescriptorBufferBindingInfoEXT trans_draw_addr =
          tb_desc_buff_get_binding(&mesh_sys->trans_draw_descs);
#else
      VkDescriptorSet opaque_draw_set = tb_rnd_frame_desc_pool_get_set(
          rnd_sys, mesh_sys->draw_pools.pools, 0);
      VkDescriptorSet trans_draw_set = tb_rnd_frame_desc_pool_get_set(
          rnd_sys, mesh_sys->draw_pools.pools, 1);
#endif

      // Opaque batch is a bit special since we need to share with the shadow
      // system
      TB_CHECK(mesh_sys->opaque_batch == NULL, "Opaque batch was not consumed");
      mesh_sys->opaque_batch = tb_alloc_tp(mesh_sys->tmp_alloc, TbDrawBatch);
      tb_auto opaque_prim_batch =
          tb_alloc_tp(mesh_sys->tmp_alloc, TbPrimitiveBatch);

      *opaque_prim_batch = (TbPrimitiveBatch){
#if TB_USE_DESC_BUFFER == 1
          .view_addr = view_addr,
          .mat_addr = mat_addr,
          .draw_addr = opaque_draw_addr,
          .obj_addr = obj_addr,
          .tex_addr = tex_addr,
          .idx_addr = idx_addr,
          .pos_addr = pos_addr,
          .norm_addr = norm_addr,
          .tan_addr = tan_addr,
          .uv0_addr = uv0_addr,
#else
          .view_set = view_set,
          .mat_set = mat_set,
          .draw_set = opaque_draw_set,
          .meshlet_set = meshlet_set,
          .tri_set = tri_set,
          .vert_set = vert_set,
          .obj_set = obj_set,
          .tex_set = tex_set,
          .idx_set = idx_set,
          .pos_set = pos_set,
          .norm_set = norm_set,
          .tan_set = tan_set,
          .uv0_set = uv0_set,
#endif
      };

      // Define batches
      *mesh_sys->opaque_batch = (TbDrawBatch){
          .layout = mesh_sys->mesh_pipe_layout,
          .pipeline = tb_shader_get_pipeline(ecs, mesh_sys->opaque_mesh_shader),
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = opaque_prim_batch,
          .draw_count = 1,
          .draw_size = sizeof(TbIndirectDraw),
          .draws = &mesh_sys->opaque_draw,
          .draw_max = 1,
      };

      TbDrawBatch trans_batch = {
          .layout = mesh_sys->mesh_pipe_layout,
          .pipeline =
              tb_shader_get_pipeline(ecs, mesh_sys->transparent_mesh_shader),
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch =
              &(TbPrimitiveBatch){
#if TB_USE_DESC_BUFFER == 1
                  .view_addr = view_addr,
                  .mat_addr = mat_addr,
                  .draw_addr = trans_draw_addr,
                  .obj_addr = obj_addr,
                  .tex_addr = tex_addr,
                  .idx_addr = idx_addr,
                  .pos_addr = pos_addr,
                  .norm_addr = norm_addr,
                  .tan_addr = tan_addr,
                  .uv0_addr = uv0_addr,
#else
                  .view_set = view_set,
                  .mat_set = mat_set,
                  .draw_set = trans_draw_set,
                  .meshlet_set = meshlet_set,
                  .tri_set = tri_set,
                  .vert_set = vert_set,
                  .obj_set = obj_set,
                  .tex_set = tex_set,
                  .idx_set = idx_set,
                  .pos_set = pos_set,
                  .norm_set = norm_set,
                  .tan_set = tan_set,
                  .uv0_set = uv0_set,
#endif
              },
          .draw_count = 1,
          .draw_size = sizeof(TbIndirectDraw),
          .draws = &mesh_sys->trans_draw,
          .draw_max = 1,
      };

      // Prepass batch is the same as opaque but with different pipeline
      tb_auto prepass_batch = *mesh_sys->opaque_batch;
      {
        prepass_batch.pipeline =
            tb_shader_get_pipeline(ecs, mesh_sys->prepass_mesh_shader);
        prepass_batch.layout = mesh_sys->prepass_mesh_layout;
      }

      {
        TB_TRACY_SCOPE("Submit Batches");
        if (!TB_DYN_ARR_EMPTY(mesh_sys->opaque_draw_data)) {
          TbDrawContextId prepass_ctx2 = mesh_sys->prepass_draw_ctx;
          tb_render_pipeline_issue_draw_batch(rp_sys, prepass_ctx2, 1,
                                              &prepass_batch);

          TbDrawContextId opaque_ctx2 = mesh_sys->opaque_draw_ctx;
          tb_render_pipeline_issue_draw_batch(rp_sys, opaque_ctx2, 1,
                                              mesh_sys->opaque_batch);
        }
        if (!TB_DYN_ARR_EMPTY(mesh_sys->trans_draw_data)) {
          TbDrawContextId trans_ctx2 = mesh_sys->transparent_draw_ctx;
          tb_render_pipeline_issue_draw_batch(rp_sys, trans_ctx2, 1,
                                              &trans_batch);
        }
      }
    }
  }
}

void tb_register_mesh_sys(TbWorld *world) {
  TB_TRACY_SCOPEC("Register Mesh Sys", TracyCategoryColorRendering);
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMeshSystem);
  ECS_TAG_DEFINE(ecs, TbMeshInGPUScene);

  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);
  tb_auto view_sys = ecs_singleton_ensure(ecs, TbViewSystem);
  tb_auto rp_sys = ecs_singleton_ensure(ecs, TbRenderPipelineSystem);

  tb_auto sys = create_mesh_system_internal(
      ecs, world->gp_alloc, world->tmp_alloc, rnd_sys, view_sys, rp_sys);
  sys.camera_query =
      ecs_query(ecs, {
                         .terms =
                             {
                                 {.id = ecs_id(TbCameraComponent)},
                             },
                         .cache_kind = EcsQueryCacheAuto,
                     });
  sys.mesh_query = ecs_query(ecs, {
                                      .terms =
                                          {
                                              {.id = ecs_id(TbMeshComponent)},
                                              {.id = ecs_id(TbRenderObject)},
                                          },
                                      .cache_kind = EcsQueryCacheAuto,
                                  });
  sys.dir_light_query =
      ecs_query(ecs, {
                         .terms =
                             {
                                 {.id = ecs_id(TbDirectionalLightComponent)},
                             },
                         .cache_kind = EcsQueryCacheAuto,
                     });

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbMeshSystem), TbMeshSystem, &sys);

  // A system to operate on newly added mesh components and sort them into
  // the proper draw list
  ecs_system(
      ecs,
      {
          .entity = ecs_entity(ecs,
                               {
                                   .name = "mesh_sort_new",
                                   .add = ecs_ids(ecs_dependson(EcsPreUpdate)),
                               }),
          .query.terms = {{.id = ecs_id(TbMeshSystem),
                           .src.id = ecs_id(TbMeshSystem)},
                          {.id = ecs_id(TbMeshComponent)},
                          {.id = ecs_id(TbRenderObject)},
                          {.id = ecs_id(TbMeshInGPUScene), .oper = EcsNot}},
          .callback = mesh_sort_new,
      });

  // This system takes recorded draws and makes sure they're on the GPU
  // Do this every frame since draws should be called on the GPU and
  // adding / removing objects to the scene is a constant thing
  ecs_system(
      ecs,
      {
          .entity = ecs_entity(ecs,
                               {
                                   .name = "mesh_construct_draws",
                                   .add = ecs_ids(ecs_dependson(EcsOnUpdate)),
                               }),
          .query.terms =
              {
                  {.id = ecs_id(TbRenderSystem),
                   .src.id = ecs_id(TbRenderSystem)},
                  {.id = ecs_id(TbMeshSystem), .src.id = ecs_id(TbMeshSystem)},
              },
          .callback = mesh_construct_draws,
      });

  // System submits draw batches
  ECS_SYSTEM(ecs, mesh_draw_tick, EcsOnStore, TbMeshSystem($));
}

void tb_unregister_mesh_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  TbMeshSystem *sys = ecs_singleton_ensure(ecs, TbMeshSystem);
  ecs_query_fini(sys->dir_light_query);
  ecs_query_fini(sys->mesh_query);
  ecs_query_fini(sys->camera_query);
  destroy_mesh_system(ecs, sys);
  ecs_singleton_remove(ecs, TbMeshSystem);
}
