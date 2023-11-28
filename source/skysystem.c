#include "skysystem.h"

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "env_filter_frag.h"
#include "env_filter_vert.h"
#include "irradiance_frag.h"
#include "irradiance_vert.h"
#include "sky_cube_frag.h"
#include "sky_cube_vert.h"
#include "sky_frag.h"
#include "sky_vert.h"
#pragma clang diagnostic pop

#include "assetsystem.h"
#include "cameracomponent.h"
#include "common.hlsli"
#include "json.h"
#include "lightcomponent.h"
#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "sky.hlsli"
#include "skycomponent.h"
#include "skydome.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>

#define FILTERED_ENV_DIM 512
#define FILTERED_ENV_MIPS ((uint32_t)(SDL_floorf(log2f(FILTERED_ENV_DIM))) + 1u)

typedef struct SkyDrawBatch {
  VkPushConstantRange const_range;
  TbSkyPushConstants consts;
  VkDescriptorSet sky_set;

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} SkyDrawBatch;

typedef struct IrradianceBatch {
  VkDescriptorSet set;

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} IrradianceBatch;

typedef struct PrefilterBatch {
  TbEnvFilterConstants consts;
  VkDescriptorSet set;

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} PrefilterBatch;

VkResult create_sky_pipeline(TbRenderSystem *render_system,
                             VkFormat color_format, VkFormat depth_format,
                             VkPipelineLayout layout, VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(sky_vert);
    create_info.pCode = (const uint32_t *)sky_vert;
    tb_rnd_create_shader(render_system, &create_info, "sky vert", &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load sky vert shader module", err);

    create_info.codeSize = sizeof(sky_frag);
    create_info.pCode = (const uint32_t *)sky_frag;
    tb_rnd_create_shader(render_system, &create_info, "sky frag", &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load sky frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
              .vertexBindingDescriptionCount = 1,
              .pVertexBindingDescriptions =
                  (VkVertexInputBindingDescription[1]){
                      {0, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
                  },
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[1]){
                      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                  },
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
              .cullMode = VK_CULL_MODE_BACK_BIT,
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
              .depthWriteEnable = VK_FALSE,
              .depthCompareOp = VK_COMPARE_OP_EQUAL,
              .maxDepthBounds = 1.0f,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Sky Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create sky pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_env_capture_pipeline(TbRenderSystem *render_system,
                                     VkFormat color_format,
                                     VkPipelineLayout layout,
                                     VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(sky_cube_vert);
    create_info.pCode = (const uint32_t *)sky_cube_vert;
    tb_rnd_create_shader(render_system, &create_info, "env capture vert",
                         &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load env capture vert shader module", err);

    create_info.codeSize = sizeof(sky_cube_frag);
    create_info.pCode = (const uint32_t *)sky_cube_frag;
    tb_rnd_create_shader(render_system, &create_info, "env capture frag",
                         &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load env capture frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .viewMask = 0x0000003F, // 0b00111111
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
              .vertexBindingDescriptionCount = 1,
              .pVertexBindingDescriptions =
                  &(VkVertexInputBindingDescription){
                      .binding = 0,
                      .stride = sizeof(float3),
                      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                  },
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  &(VkVertexInputAttributeDescription){
                      .binding = 0,
                      .location = 0,
                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                      .offset = 0,
                  },
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
              .cullMode = VK_CULL_MODE_BACK_BIT,
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
      .layout = layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Env Capture Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to env capture pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_irradiance_pipeline(TbRenderSystem *render_system,
                                    VkFormat color_format,
                                    VkPipelineLayout layout,
                                    VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(irradiance_vert);
    create_info.pCode = (const uint32_t *)irradiance_vert;
    tb_rnd_create_shader(render_system, &create_info, "Irradiance vert",
                         &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load irradiance vert shader module", err);

    create_info.codeSize = sizeof(irradiance_frag);
    create_info.pCode = (const uint32_t *)irradiance_frag;
    tb_rnd_create_shader(render_system, &create_info, "Irradiance frag",
                         &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load irradiance frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .viewMask = 0x0000003F, // 0b00111111
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
              .vertexBindingDescriptionCount = 1,
              .pVertexBindingDescriptions =
                  &(VkVertexInputBindingDescription){
                      .binding = 0,
                      .stride = sizeof(float3),
                      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                  },
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  &(VkVertexInputAttributeDescription){
                      .binding = 0,
                      .location = 0,
                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                      .offset = 0,
                  },
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
              .cullMode = VK_CULL_MODE_BACK_BIT,
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
      .layout = layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Irradiance Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create irradiance pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_prefilter_pipeline(TbRenderSystem *render_system,
                                   VkFormat color_format,
                                   VkPipelineLayout layout,
                                   VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(env_filter_vert);
    create_info.pCode = (const uint32_t *)env_filter_vert;
    tb_rnd_create_shader(render_system, &create_info, "Env Filter vert",
                         &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load env prefilter vert shader module",
                    err);

    create_info.codeSize = sizeof(env_filter_frag);
    create_info.pCode = (const uint32_t *)env_filter_frag;
    tb_rnd_create_shader(render_system, &create_info, "Env Filter frag",
                         &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load env prefilter frag shader module",
                    err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .viewMask = 0x0000003F, // 0b00111111
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
              .vertexBindingDescriptionCount = 1,
              .pVertexBindingDescriptions =
                  &(VkVertexInputBindingDescription){
                      .binding = 0,
                      .stride = sizeof(float3),
                      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                  },
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  &(VkVertexInputAttributeDescription){
                      .binding = 0,
                      .location = 0,
                      .format = VK_FORMAT_R32G32B32_SFLOAT,
                      .offset = 0,
                  },
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
              .cullMode = VK_CULL_MODE_BACK_BIT,
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
      .layout = layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Prefilter Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create prefilter pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

void record_sky_common(VkCommandBuffer buffer, uint32_t batch_count,
                       const TbDrawBatch *batches) {
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const SkyDrawBatch *sky_batch = (const SkyDrawBatch *)batch->user_batch;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    VkPushConstantRange range = sky_batch->const_range;
    const TbSkyPushConstants *consts = &sky_batch->consts;
    vkCmdPushConstants(buffer, batch->layout, range.stageFlags, range.offset,
                       range.size, consts);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &sky_batch->sky_set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, sky_batch->geom_buffer, 0,
                         VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &sky_batch->geom_buffer,
                           &sky_batch->vertex_offset);
    vkCmdDrawIndexed(buffer, sky_batch->index_count, 1, 0, 0, 0);
  }
}

void record_sky(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Sky Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Sky", 3, true);
  cmd_begin_label(buffer, "Sky", (float4){0.8f, 0.8f, 0.0f, 1.0f});

  record_sky_common(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_env_capture(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Env Capture Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Env Capture", 3, true);
  cmd_begin_label(buffer, "Env Capture", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  record_sky_common(buffer, batch_count, batches);

  // TODO: After capturing the environment map we need to mip it down

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_irradiance(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Irradiance Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Irradiance", 3, true);
  cmd_begin_label(buffer, "Irradiance", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  for (uint32_t i = 0; i < batch_count; ++i) {
    const TbDrawBatch *batch = &batches[i];
    const IrradianceBatch *irr_batch =
        (const IrradianceBatch *)batch->user_batch;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &irr_batch->set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, irr_batch->geom_buffer, 0,
                         VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &irr_batch->geom_buffer,
                           &irr_batch->vertex_offset);
    vkCmdDrawIndexed(buffer, irr_batch->index_count, 1, 0, 0, 0);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_env_filter(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Env Filter Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Env Filter", 3, true);
  cmd_begin_label(buffer, "Env Filter", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  for (uint32_t i = 0; i < batch_count; ++i) {
    const TbDrawBatch *batch = &batches[i];
    const PrefilterBatch *pre_batch = (const PrefilterBatch *)batch->user_batch;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &pre_batch->set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, pre_batch->geom_buffer, 0,
                         VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &pre_batch->geom_buffer,
                           &pre_batch->vertex_offset);

    vkCmdPushConstants(buffer, batch->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(TbEnvFilterConstants), &pre_batch->consts);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdDrawIndexed(buffer, pre_batch->index_count, 1, 0, 0, 0);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

TbSkySystem create_sky_system(TbAllocator std_alloc, TbAllocator tmp_alloc,
                              TbRenderSystem *render_system,
                              TbRenderPipelineSystem *render_pipe_system,
                              TbRenderTargetSystem *render_target_system,
                              TbViewSystem *view_system) {
  TbSkySystem sys = (TbSkySystem){
      .render_system = render_system,
      .render_pipe_system = render_pipe_system,
      .render_target_system = render_target_system,
      .view_system = view_system,
      .tmp_alloc = tmp_alloc,
      .std_alloc = std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Get passes
  TbRenderPassId sky_pass_id = render_pipe_system->sky_pass;
  TbRenderPassId irr_pass_id = render_pipe_system->irradiance_pass;

  // Create irradiance sampler
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
        .maxLod = FILTERED_ENV_MIPS,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    err = tb_rnd_create_sampler(render_system, &create_info,
                                "Irradiance Sampler", &sys.irradiance_sampler);
    TB_VK_CHECK(err, "Failed to create irradiance sampler");
  }

  // Create Descriptor Set Layouts
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings =
            &(VkDescriptorSetLayoutBinding){
                0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
    };
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "Sky Set Layout", &sys.sky_set_layout);
    TB_VK_CHECK(err, "Failed to create sky descriptor set layout");
  }
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings =
            (VkDescriptorSetLayoutBinding[2]){
                {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &sys.irradiance_sampler},
            },
    };
    err =
        tb_rnd_create_set_layout(render_system, &create_info,
                                 "Irradiance Set Layout", &sys.irr_set_layout);
    TB_VK_CHECK(err, "Failed to irradiance sky descriptor set layout");
  }

  // Create Pipeline Layouts
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sys.sky_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            &(VkPushConstantRange){
                VK_SHADER_STAGE_ALL_GRAPHICS,
                0,
                sizeof(TbSkyPushConstants),
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Sky Pipeline Layout",
                                        &sys.sky_pipe_layout);
    TB_VK_CHECK(err, "Failed to create sky pipeline layout");
  }
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sys.irr_set_layout,
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Irradiance Pipeline Layout",
                                        &sys.irr_pipe_layout);
    TB_VK_CHECK(err, "Failed to create irradiance pipeline layout");
  }
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sys.irr_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            &(VkPushConstantRange){
                .size = sizeof(TbEnvFilterConstants),
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Prefilter Pipeline Layout",
                                        &sys.prefilter_pipe_layout);
    TB_VK_CHECK(err, "Failed to create prefilter pipeline layout");
  }

  // Look up target color and depth formats for pipeline creation
  {
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;

    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(render_pipe_system, sky_pass_id,
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 2, "Unexpected");
    TbPassAttachment attach_info[2] = {0};
    tb_render_pipeline_get_attachments(render_pipe_system, sky_pass_id,
                                       &attach_count, attach_info);

    for (uint32_t attach_idx = 0; attach_idx < attach_count; ++attach_idx) {
      VkFormat format = tb_render_target_get_format(
          render_target_system, attach_info[attach_idx].attachment);
      if (format == VK_FORMAT_D32_SFLOAT) {
        depth_format = format;
      } else {
        color_format = format;
      }
    }

    // Create sky pipeline
    err = create_sky_pipeline(render_system, color_format, depth_format,
                              sys.sky_pipe_layout, &sys.sky_pipeline);
    TB_VK_CHECK(err, "Failed to create sky pipeline");
  }

  // Create env capture pipeline

  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(render_pipe_system,
                                       render_pipe_system->env_cap_passes[0],
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexepcted");
    TbPassAttachment attach_info = {0};
    tb_render_pipeline_get_attachments(render_pipe_system,
                                       render_pipe_system->env_cap_passes[0],
                                       &attach_count, &attach_info);

    VkFormat color_format = tb_render_target_get_format(render_target_system,
                                                        attach_info.attachment);
    err = create_env_capture_pipeline(render_system, color_format,
                                      sys.sky_pipe_layout, &sys.env_pipeline);
    TB_VK_CHECK(err, "Failed to create env capture pipeline");
  }

  // Create irradiance pipeline
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(render_pipe_system,
                                       render_pipe_system->irradiance_pass,
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexepcted");
    TbPassAttachment attach_info = {0};
    tb_render_pipeline_get_attachments(render_pipe_system,
                                       render_pipe_system->irradiance_pass,
                                       &attach_count, &attach_info);

    VkFormat color_format = tb_render_target_get_format(render_target_system,
                                                        attach_info.attachment);
    err = create_irradiance_pipeline(render_system, color_format,
                                     sys.irr_pipe_layout,
                                     &sys.irradiance_pipeline);
    TB_VK_CHECK(err, "Failed to create irradiance pipeline");
  }

  // Create prefilter pipeline
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(render_pipe_system,
                                       render_pipe_system->prefilter_passes[0],
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexepcted");
    TbPassAttachment attach_info = {0};
    tb_render_pipeline_get_attachments(render_pipe_system,
                                       render_pipe_system->prefilter_passes[0],
                                       &attach_count, &attach_info);

    VkFormat color_format = tb_render_target_get_format(render_target_system,
                                                        attach_info.attachment);
    err = create_prefilter_pipeline(render_system, color_format,
                                    sys.prefilter_pipe_layout,
                                    &sys.prefilter_pipeline);
    TB_VK_CHECK(err, "Failed to create prefilter pipeline");
  }

  // Register passes with the render system
  sys.sky_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(TbDrawContextDescriptor){
                              .batch_size = sizeof(SkyDrawBatch),
                              .draw_fn = record_sky,
                              .pass_id = sky_pass_id,
                          });
  sys.irradiance_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(TbDrawContextDescriptor){
                              .batch_size = sizeof(IrradianceBatch),
                              .draw_fn = record_irradiance,
                              .pass_id = irr_pass_id,
                          });
  for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
    sys.env_capture_ctxs[i] = tb_render_pipeline_register_draw_context(
        render_pipe_system,
        &(TbDrawContextDescriptor){
            .batch_size = sizeof(SkyDrawBatch),
            .draw_fn = record_env_capture,
            .pass_id = render_pipe_system->env_cap_passes[i],
        });
    sys.prefilter_ctxs[i] = tb_render_pipeline_register_draw_context(
        render_pipe_system,
        &(TbDrawContextDescriptor){
            .batch_size = sizeof(PrefilterBatch),
            .draw_fn = record_env_filter,
            .pass_id = render_pipe_system->prefilter_passes[i],
        });
  }

  // Create skydome geometry
  {
    const uint64_t skydome_size = get_skydome_size();
    // Use the gpu tmp buffer to copy the geom buffer
    {
      void *ptr = NULL;
      // Make space for the sky geometry on the GPU
      {
        VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .size = skydome_size,
        };
        err = tb_rnd_sys_create_gpu_buffer_tmp(
            render_system, &create_info, "SkyDome Geom Buffer",
            &sys.sky_geom_gpu_buffer, 16, &ptr);
        TB_VK_CHECK(err, "Failed to create skydome geom buffer");
      }
      copy_skydome(ptr);
    }
  }

  return sys;
}

void destroy_sky_system(TbSkySystem *self) {
  TbRenderSystem *render_system = self->render_system;

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(render_system,
                                   self->frame_states[i].set_pool);
  }

  vmaDestroyBuffer(render_system->vma_alloc, self->sky_geom_gpu_buffer.buffer,
                   self->sky_geom_gpu_buffer.alloc);

  tb_rnd_destroy_set_layout(render_system, self->sky_set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->sky_pipe_layout);
  tb_rnd_destroy_set_layout(render_system, self->irr_set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->irr_pipe_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->prefilter_pipe_layout);
  tb_rnd_destroy_sampler(render_system, self->irradiance_sampler);
  tb_rnd_destroy_pipeline(render_system, self->sky_pipeline);
  tb_rnd_destroy_pipeline(render_system, self->env_pipeline);
  tb_rnd_destroy_pipeline(render_system, self->irradiance_pipeline);
  tb_rnd_destroy_pipeline(render_system, self->prefilter_pipeline);

  *self = (TbSkySystem){0};
}

void sky_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Sky Draw", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, TbSkySystem);
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbSkyComponent);
  ECS_COMPONENT(ecs, TbCameraComponent);
  ECS_COMPONENT(ecs, TbTransformComponent);

  TbSkySystem *sky_sys = ecs_singleton_get_mut(ecs, TbSkySystem);
  sky_sys->time += it->delta_time;
  ecs_singleton_modified(ecs, TbSkySystem);

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  ecs_singleton_modified(ecs, TbRenderSystem);
  TbRenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  ecs_singleton_modified(ecs, TbRenderPipelineSystem);

  // TODO: Make this less hacky
  const uint32_t width = rnd_sys->render_thread->swapchain.width;
  const uint32_t height = rnd_sys->render_thread->swapchain.height;

  TbSkySystemFrameState *state = &sky_sys->frame_states[rnd_sys->frame_idx];

  // Write descriptor sets for each sky
  TbSkyComponent *skys = ecs_field(it, TbSkyComponent, 1);
  for (int32_t sky_idx = 0; sky_idx < it->count; ++sky_idx) {
    TbSkyComponent *sky = &skys[sky_idx];

    VkResult err = VK_SUCCESS;
    VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys);

    const uint32_t write_count = 2; // +1 for irradiance pass

    // Allocate all the descriptor sets for this frame
    {
      // Resize the pool
      if (state->set_count < write_count) {
        if (state->set_pool) {
          tb_rnd_destroy_descriptor_pool(rnd_sys, state->set_pool);
        }

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = write_count,
            .poolSizeCount = 2,
            .pPoolSizes =
                (VkDescriptorPoolSize[2]){
                    {
                        .descriptorCount = write_count,
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    },
                    {
                        .descriptorCount = write_count,
                        .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    },
                },
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        };
        err = tb_rnd_create_descriptor_pool(
            rnd_sys, &create_info, "Sky System Frame State Descriptor Pool",
            &state->set_pool);
        TB_VK_CHECK(err,
                    "Failed to create sky system frame state descriptor pool");

        state->set_count = write_count;
        state->sets = tb_realloc_nm_tp(sky_sys->std_alloc, state->sets,
                                       state->set_count, VkDescriptorSet);
      } else {
        vkResetDescriptorPool(rnd_sys->render_thread->device, state->set_pool,
                              0);
        state->set_count = write_count;
      }

      VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
          sky_sys->tmp_alloc, write_count, VkDescriptorSetLayout);
      layouts[0] = sky_sys->sky_set_layout;
      layouts[1] = sky_sys->irr_set_layout;

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = state->set_count,
          .descriptorPool = state->set_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(rnd_sys->render_thread->device,
                                     &alloc_info, state->sets);
      TB_VK_CHECK(err, "Failed to re-allocate sky descriptor sets");
    }

    // Just upload and write all views for now, they tend to be important anyway
    VkWriteDescriptorSet *writes =
        tb_alloc_nm_tp(sky_sys->tmp_alloc, write_count, VkWriteDescriptorSet);
    VkDescriptorBufferInfo *buffer_info =
        tb_alloc_tp(sky_sys->tmp_alloc, VkDescriptorBufferInfo);

    TbSkyData data = {
        .time = sky_sys->time,
        .cirrus = sky->cirrus,
        .cumulus = sky->cumulus,
        .sun_dir = sky->sun_dir,
    };

    // Write view data into the tmp buffer we know will wind up on the GPU
    uint64_t offset = 0;
    err = tb_rnd_sys_tmp_buffer_copy(rnd_sys, sizeof(TbSkyData), 0x40, &data,
                                     &offset);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for sky");

    *buffer_info = (VkDescriptorBufferInfo){
        .buffer = tmp_gpu_buffer,
        .offset = offset,
        .range = sizeof(TbSkyData),
    };

    // Construct a write descriptor
    writes[0] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = state->sets[0],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = buffer_info,
    };

    // Last write is for the irradiance pass
    VkImageView env_map_view = tb_render_target_get_view(
        sky_sys->render_target_system, rnd_sys->frame_idx,
        sky_sys->render_target_system->env_cube);
    writes[1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = state->sets[1],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo =
            &(VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = env_map_view,
            },
    };
    tb_rnd_update_descriptors(rnd_sys, write_count, writes);
  }

  ecs_iter_t cam_it = ecs_query_iter(ecs, sky_sys->camera_query);
  while (ecs_query_next(&cam_it)) {
    TbCameraComponent *cameras = ecs_field(&cam_it, TbCameraComponent, 1);
    TbTransformComponent *transforms =
        ecs_field(&cam_it, TbTransformComponent, 2);
    for (int32_t cam_idx = 0; cam_idx < cam_it.count; ++cam_idx) {
      TbCameraComponent *camera = &cameras[cam_idx];
      TbTransformComponent *transform = &transforms[cam_idx];

      // Need to manually calculate this here
      float4x4 vp = {.col0 = {0}};
      {
        float4x4 proj = tb_perspective(camera->fov, camera->aspect_ratio,
                                       camera->near, camera->far);
        float3 forward = tb_transform_get_forward(&transform->transform);
        float4x4 view = tb_look_forward(TB_ORIGIN, forward, TB_UP);
        vp = tb_mulf44f44(proj, view);
      }

      for (int32_t sky_idx = 0; sky_idx < it->count; ++sky_idx) {
        SkyDrawBatch *sky_batch = tb_alloc_tp(sky_sys->tmp_alloc, SkyDrawBatch);
        *sky_batch = (SkyDrawBatch){
            .const_range =
                (VkPushConstantRange){
                    .size = sizeof(TbSkyPushConstants),
                    .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                },
            .consts =
                {
                    .vp = vp,
                },
            .sky_set = state->sets[0],
            .geom_buffer = sky_sys->sky_geom_gpu_buffer.buffer,
            .index_count = get_skydome_index_count(),
            .vertex_offset = get_skydome_vert_offset(),
        };
        TbDrawBatch *sky_draw_batch =
            tb_alloc_tp(sky_sys->tmp_alloc, TbDrawBatch);
        *sky_draw_batch = (TbDrawBatch){
            .layout = sky_sys->sky_pipe_layout,
            .pipeline = sky_sys->sky_pipeline,
            .viewport = {0, height, width, -(float)height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .user_batch = sky_batch,
        };

        TbDrawBatch *env_draw_batches = tb_alloc_nm_tp(
            sky_sys->tmp_alloc, PREFILTER_PASS_COUNT, TbDrawBatch);

        // We need to capture the environment once per
        // pre-filtered reflection mip in order to
        // avoid sun intensity artifacts
        for (uint32_t env_idx = 0; env_idx < PREFILTER_PASS_COUNT; ++env_idx) {
          const float mip_dim = FILTERED_ENV_DIM * SDL_powf(0.5f, env_idx);
          env_draw_batches[env_idx] = (TbDrawBatch){
              .layout = sky_sys->sky_pipe_layout,
              .pipeline = sky_sys->env_pipeline,
              .viewport = {0, mip_dim, mip_dim, -mip_dim, 0, 1},
              .scissor = {{0, 0}, {mip_dim, mip_dim}},
              .user_batch = sky_batch,
          };
        }

        // Generate the batch for the irradiance pass
        TbDrawBatch *irr_draw_batch =
            tb_alloc_tp(sky_sys->tmp_alloc, TbDrawBatch);
        IrradianceBatch *irradiance_batch =
            tb_alloc_tp(sky_sys->tmp_alloc, IrradianceBatch);
        {
          *irr_draw_batch = (TbDrawBatch){
              .layout = sky_sys->irr_pipe_layout,
              .pipeline = sky_sys->irradiance_pipeline,
              .viewport = {0, 64, 64, -64, 0, 1},
              .scissor = {{0, 0}, {64, 64}},
              .user_batch = irradiance_batch,
          };
          *irradiance_batch = (IrradianceBatch){
              .set = state->sets[state->set_count - 1],
              .geom_buffer = sky_sys->sky_geom_gpu_buffer.buffer,
              .index_count = get_skydome_index_count(),
              .vertex_offset = get_skydome_vert_offset(),
          };
        }

        // Generate batch for prefiltering the environment map
        TbDrawBatch *pre_draw_batches = tb_alloc_nm_tp(
            sky_sys->tmp_alloc, PREFILTER_PASS_COUNT, TbDrawBatch);
        PrefilterBatch *prefilter_batches = tb_alloc_nm_tp(
            sky_sys->tmp_alloc, PREFILTER_PASS_COUNT, PrefilterBatch);
        {
          for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
            const float mip_dim = FILTERED_ENV_DIM * SDL_powf(0.5f, i);

            pre_draw_batches[i] = (TbDrawBatch){
                .layout = sky_sys->prefilter_pipe_layout,
                .pipeline = sky_sys->prefilter_pipeline,
                .viewport = {0, mip_dim, mip_dim, -mip_dim, 0, 1},
                .scissor = {{0, 0}, {mip_dim, mip_dim}},
                .user_batch = &prefilter_batches[i],
            };

            prefilter_batches[i] = (PrefilterBatch){
                .set = state->sets[state->set_count - 1],
                .geom_buffer = sky_sys->sky_geom_gpu_buffer.buffer,
                .index_count = get_skydome_index_count(),
                .vertex_offset = get_skydome_vert_offset(),
                .consts =
                    {
                        .roughness = (float)i / (float)(FILTERED_ENV_MIPS - 1),
                        .sample_count = 16,
                    },
            };
          }
        }

        tb_render_pipeline_issue_draw_batch(rp_sys, sky_sys->sky_draw_ctx, 1,
                                            sky_draw_batch);

        tb_render_pipeline_issue_draw_batch(rp_sys, sky_sys->irradiance_ctx, 1,
                                            irr_draw_batch);
        for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
          tb_render_pipeline_issue_draw_batch(
              rp_sys, sky_sys->env_capture_ctxs[i], 1, &env_draw_batches[i]);
          tb_render_pipeline_issue_draw_batch(
              rp_sys, sky_sys->prefilter_ctxs[i], 1, &pre_draw_batches[i]);
        }
      }
    }
  }
  TracyCZoneEnd(ctx);
}

void tb_register_sky_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbRenderTargetSystem);
  ECS_COMPONENT(ecs, TbViewSystem);
  ECS_COMPONENT(ecs, TbSkySystem);
  ECS_COMPONENT(ecs, TbAssetSystem);
  ECS_COMPONENT(ecs, TbSkyComponent);
  ECS_COMPONENT(ecs, TbDirectionalLightComponent);
  ECS_COMPONENT(ecs, TbCameraComponent);
  ECS_COMPONENT(ecs, TbTransformComponent);

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  TbRenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  TbRenderTargetSystem *rt_sys =
      ecs_singleton_get_mut(ecs, TbRenderTargetSystem);
  TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  TbSkySystem sys = create_sky_system(world->std_alloc, world->tmp_alloc,
                                      rnd_sys, rp_sys, rt_sys, view_sys);
  sys.camera_query = ecs_query(ecs, {.filter.terms = {
                                         {.id = ecs_id(TbCameraComponent)},
                                         {.id = ecs_id(TbTransformComponent)},
                                     }});

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbSkySystem), TbSkySystem, &sys);

  ECS_SYSTEM(ecs, sky_draw_tick, EcsOnUpdate, TbSkyComponent);

  tb_register_sky_component(world);
}

void tb_unregister_sky_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbSkySystem);
  TbSkySystem *sys = ecs_singleton_get_mut(ecs, TbSkySystem);
  ecs_query_fini(sys->camera_query);
  destroy_sky_system(sys);
  ecs_singleton_remove(ecs, TbSkySystem);
}
