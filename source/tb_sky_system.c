#include "tb_sky_system.h"

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "tb_env_filter_frag.h"
#include "tb_env_filter_vert.h"
#include "tb_irradiance_frag.h"
#include "tb_irradiance_vert.h"
#include "tb_sky_cube_frag.h"
#include "tb_sky_cube_vert.h"
#include "tb_sky_frag.h"
#include "tb_sky_vert.h"
#pragma clang diagnostic pop

#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_common.slangh"
#include "tb_light_component.h"
#include "tb_profiling.h"
#include "tb_render_pipeline_system.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_shader_system.h"
#include "tb_sky.slangh"
#include "tb_sky_component.h"
#include "tb_skydome.h"
#include "tb_transform_component.h"
#include "tb_view_system.h"
#include "tb_world.h"

#include <flecs.h>
#include <json.h>
#include <math.h>

#define FILTERED_ENV_DIM 512
#define FILTERED_ENV_MIPS ((uint32_t)(SDL_floorf(log2f(FILTERED_ENV_DIM))) + 1u)

typedef VkDescriptorBufferBindingInfoEXT TbSkyDescInfo;
ECS_COMPONENT_DECLARE(TbSkyDescInfo);

ECS_COMPONENT_DECLARE(TbSkySystem);

ECS_TAG_DECLARE(TbSkyRenderDirty);

typedef struct SkyDrawBatch {
  VkPushConstantRange const_range;
  TbSkyPushConstants consts;
#if TB_USE_DESC_BUFFER == 1
  VkDescriptorBufferBindingInfoEXT desc_binding;
#else
  VkDescriptorSet sky_set;
#endif

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} SkyDrawBatch;

typedef struct IrradianceBatch {
#if TB_USE_DESC_BUFFER == 1
  VkDescriptorBufferBindingInfoEXT desc_binding;
#else
  VkDescriptorSet set;
#endif

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} IrradianceBatch;

typedef struct PrefilterBatch {
  TbEnvFilterConstants consts;
#if TB_USE_DESC_BUFFER == 1
  VkDescriptorBufferBindingInfoEXT desc_binding;
#else
  VkDescriptorSet set;
#endif

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} PrefilterBatch;

void tb_register_sky_sys(TbWorld *world);
void tb_unregister_sky_sys(TbWorld *world);

TB_REGISTER_SYS(tb, sky, TB_SKY_SYS_PRIO)

typedef struct TbSkyShaderArgs {
  TbRenderSystem *rnd_sys;
  VkFormat color_format;
  VkFormat depth_format;
  VkPipelineLayout layout;
} TbSkyShaderArgs;

typedef struct TbEnvShaderArgs {
  TbRenderSystem *rnd_sys;
  VkFormat color_format;
  VkPipelineLayout layout;
} TbEnvShaderArgs;

VkPipeline create_sky_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Sky Pipeline");
  tb_auto sky_args = (TbSkyShaderArgs *)args;

  tb_auto rnd_sys = sky_args->rnd_sys;
  tb_auto color_format = sky_args->color_format;
  tb_auto depth_format = sky_args->depth_format;
  tb_auto layout = sky_args->layout;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(tb_sky_vert);
    create_info.pCode = (const uint32_t *)tb_sky_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "sky vert", &vert_mod);

    create_info.codeSize = sizeof(tb_sky_frag);
    create_info.pCode = (const uint32_t *)tb_sky_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "sky frag", &frag_mod);
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info, "Sky Pipeline",
                                   &pipeline);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_env_capture_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Env Capture Pipeline");
  tb_auto env_args = (TbEnvShaderArgs *)args;

  tb_auto rnd_sys = env_args->rnd_sys;
  tb_auto color_format = env_args->color_format;
  tb_auto layout = env_args->layout;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(tb_sky_cube_vert);
    create_info.pCode = (const uint32_t *)tb_sky_cube_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "env capture vert", &vert_mod);

    create_info.codeSize = sizeof(tb_sky_cube_frag);
    create_info.pCode = (const uint32_t *)tb_sky_cube_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "env capture frag", &frag_mod);
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
              .viewMask = 0x0000003F, // 0b00111111
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Env Capture Pipeline", &pipeline);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_irradiance_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Irradiance Pipeline");
  tb_auto env_args = (TbEnvShaderArgs *)args;

  tb_auto rnd_sys = env_args->rnd_sys;
  tb_auto color_format = env_args->color_format;
  tb_auto layout = env_args->layout;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(tb_irradiance_vert);
    create_info.pCode = (const uint32_t *)tb_irradiance_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Irradiance vert", &vert_mod);

    create_info.codeSize = sizeof(tb_irradiance_frag);
    create_info.pCode = (const uint32_t *)tb_irradiance_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Irradiance frag", &frag_mod);
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
              .viewMask = 0x0000003F, // 0b00111111
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Irradiance Pipeline", &pipeline);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_prefilter_pipeline(void *args) {
  TB_TRACY_SCOPE("Create Prefilter Pipeline");
  tb_auto env_args = (TbEnvShaderArgs *)args;

  tb_auto rnd_sys = env_args->rnd_sys;
  tb_auto color_format = env_args->color_format;
  tb_auto layout = env_args->layout;

  // Load Shaders
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(tb_env_filter_vert);
    create_info.pCode = (const uint32_t *)tb_env_filter_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Env Filter vert", &vert_mod);

    create_info.codeSize = sizeof(tb_env_filter_frag);
    create_info.pCode = (const uint32_t *)tb_env_filter_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Env Filter frag", &frag_mod);
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
              .viewMask = 0x0000003F, // 0b00111111
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Prefilter Pipeline", &pipeline);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
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

#if TB_USE_DESC_BUFFER == 1
    vkCmdBindDescriptorBuffersEXT(buffer, 1, &sky_batch->desc_binding);
    uint32_t buf_indices[1] = {0};
    VkDeviceSize buf_offsets[1] = {0};
    vkCmdSetDescriptorBufferOffsetsEXT(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       batch->layout, 0, 1, buf_indices,
                                       buf_offsets);
#else
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &sky_batch->sky_set, 0, NULL);
#endif

    vkCmdBindIndexBuffer(buffer, sky_batch->geom_buffer, 0,
                         VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &sky_batch->geom_buffer,
                           &sky_batch->vertex_offset);
    vkCmdDrawIndexed(buffer, sky_batch->index_count, 1, 0, 0, 0);
  }
}

void record_sky(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                uint32_t batch_count, const TbDrawBatch *batches) {
  TB_TRACY_SCOPEC("Sky Record", TracyCategoryColorRendering);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Sky", 3, true);
  cmd_begin_label(buffer, "Sky", (float4){0.8f, 0.8f, 0.0f, 1.0f});

  record_sky_common(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
}

/*
  What's with the commented out TracyCVkNamedZone calls?
  Passes that use multi-view can occasionally step out of phase with the
  Tracy query ringbuffer and some invalid number of bits will be requested
  before the query count can wrap around back to 0
*/

void record_env_capture(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  (void)gpu_ctx;
  TB_TRACY_SCOPEC("Env Capture Record", TracyCategoryColorRendering);
  // TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Env Capture", 3, true);
  cmd_begin_label(buffer, "Env Capture", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  record_sky_common(buffer, batch_count, batches);

  // TODO: After capturing the environment map we need to mip it down

  cmd_end_label(buffer);
  // TracyCVkZoneEnd(frame_scope);
}

void record_irradiance(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  (void)gpu_ctx;
  TB_TRACY_SCOPEC("Irradiance Record", TracyCategoryColorRendering);
  // TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Irradiance", 3, true);
  cmd_begin_label(buffer, "Irradiance", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  for (uint32_t i = 0; i < batch_count; ++i) {
    const TbDrawBatch *batch = &batches[i];
    const IrradianceBatch *irr_batch =
        (const IrradianceBatch *)batch->user_batch;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

#if TB_USE_DESC_BUFFER == 1
    vkCmdBindDescriptorBuffersEXT(buffer, 1, &irr_batch->desc_binding);
    uint32_t buf_indices[1] = {0};
    VkDeviceSize buf_offsets[1] = {0};
    vkCmdSetDescriptorBufferOffsetsEXT(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       batch->layout, 0, 1, buf_indices,
                                       buf_offsets);
#else
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &irr_batch->set, 0, NULL);
#endif

    vkCmdBindIndexBuffer(buffer, irr_batch->geom_buffer, 0,
                         VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &irr_batch->geom_buffer,
                           &irr_batch->vertex_offset);
    vkCmdDrawIndexed(buffer, irr_batch->index_count, 1, 0, 0, 0);
  }

  cmd_end_label(buffer);
  // TracyCVkZoneEnd(frame_scope);
}

void record_env_filter(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  (void)gpu_ctx;
  TB_TRACY_SCOPEC("Env Filter Record", TracyCategoryColorRendering);
  // TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Env Filter", 3, true);
  cmd_begin_label(buffer, "Env Filter", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  for (uint32_t i = 0; i < batch_count; ++i) {
    const TbDrawBatch *batch = &batches[i];
    const PrefilterBatch *pre_batch = (const PrefilterBatch *)batch->user_batch;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

#if TB_USE_DESC_BUFFER == 1
    vkCmdBindDescriptorBuffersEXT(buffer, 1, &pre_batch->desc_binding);
    uint32_t buf_indices[1] = {0};
    VkDeviceSize buf_offsets[1] = {0};
    vkCmdSetDescriptorBufferOffsetsEXT(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       batch->layout, 0, 1, buf_indices,
                                       buf_offsets);
#else
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &pre_batch->set, 0, NULL);
#endif

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
  // TracyCVkZoneEnd(frame_scope);
}

TbSkySystem create_sky_system(ecs_world_t *ecs, TbAllocator gp_alloc,
                              TbAllocator tmp_alloc, TbRenderSystem *rnd_sys,
                              TbRenderPipelineSystem *rp_sys,
                              TbRenderTargetSystem *rt_sys,
                              TbViewSystem *view_sys) {
  TbSkySystem sys = (TbSkySystem){
      .rnd_sys = rnd_sys,
      .rp_sys = rp_sys,
      .rt_sys = rt_sys,
      .view_sys = view_sys,
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
  };

  // Get passes
  TbRenderPassId sky_pass_id = rp_sys->sky_pass;
  TbRenderPassId irr_pass_id = rp_sys->irradiance_pass;

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
    tb_rnd_create_sampler(rnd_sys, &create_info, "Irradiance Sampler",
                          &sys.irradiance_sampler);
  }

  VkFlags layout_flags =
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
#if TB_USE_DESC_BUFFER == 1
  layout_flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
#endif

  // Create Descriptor Set Layouts
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = layout_flags,
        .bindingCount = 1,
        .pBindings =
            &(VkDescriptorSetLayoutBinding){
                0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Sky Set Layout",
                             &sys.sky_set_layout);
#if TB_USE_DESC_BUFFER == 1
    tb_create_descriptor_buffer(rnd_sys, sys.sky_set_layout, "Sky Desc Buffer",
                                1, &sys.sky_desc_buffer);
#endif
  }
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = layout_flags,
        .bindingCount = 2,
        .pBindings =
            (VkDescriptorSetLayoutBinding[2]){
                {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &sys.irradiance_sampler},
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Irradiance Set Layout",
                             &sys.irr_set_layout);
#if TB_USE_DESC_BUFFER == 1
    tb_create_descriptor_buffer(rnd_sys, sys.irr_set_layout,
                                "Irradiance Desc Buffer", 1,
                                &sys.irr_desc_buffer);
#endif
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
    tb_rnd_create_pipeline_layout(rnd_sys, &create_info, "Sky Pipeline Layout",
                                  &sys.sky_pipe_layout);
  }
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sys.irr_set_layout,
    };
    tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                  "Irradiance Pipeline Layout",
                                  &sys.irr_pipe_layout);
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
    tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                  "Prefilter Pipeline Layout",
                                  &sys.prefilter_pipe_layout);
  }

  // Look up target color and depth formats for pipeline creation
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(rp_sys, sky_pass_id, &attach_count,
                                       NULL);
    TB_CHECK(attach_count == 2, "Unexpected");
    TbPassAttachment attach_info[2] = {0};
    tb_render_pipeline_get_attachments(rp_sys, sky_pass_id, &attach_count,
                                       attach_info);

    TbSkyShaderArgs args = {
        .rnd_sys = rnd_sys,
        .layout = sys.sky_pipe_layout,
    };

    for (uint32_t attach_idx = 0; attach_idx < attach_count; ++attach_idx) {
      VkFormat format = tb_render_target_get_format(
          rt_sys, attach_info[attach_idx].attachment);
      if (format == VK_FORMAT_D32_SFLOAT) {
        args.depth_format = format;
      } else {
        args.color_format = format;
      }
    }

    // Create sky pipeline
    sys.sky_shader = tb_shader_load(ecs, create_sky_pipeline, &args,
                                    sizeof(TbSkyShaderArgs));
  }

  // Create env capture pipeline
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->env_cap_passes[0],
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexepcted");
    TbPassAttachment attach_info = {0};
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->env_cap_passes[0],
                                       &attach_count, &attach_info);

    TbEnvShaderArgs args = {
        .rnd_sys = rnd_sys,
        .layout = sys.sky_pipe_layout,
        .color_format =
            tb_render_target_get_format(rt_sys, attach_info.attachment),
    };
    sys.env_shader = tb_shader_load(ecs, create_env_capture_pipeline, &args,
                                    sizeof(TbEnvShaderArgs));
  }

  // Create irradiance pipeline
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->irradiance_pass,
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexepcted");
    TbPassAttachment attach_info = {0};
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->irradiance_pass,
                                       &attach_count, &attach_info);

    TbEnvShaderArgs args = {
        .rnd_sys = rnd_sys,
        .layout = sys.irr_pipe_layout,
        .color_format =
            tb_render_target_get_format(rt_sys, attach_info.attachment),
    };
    sys.irradiance_shader = tb_shader_load(ecs, create_irradiance_pipeline,
                                           &args, sizeof(TbEnvShaderArgs));
  }

  // Create prefilter pipeline
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->prefilter_passes[0],
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexepcted");
    TbPassAttachment attach_info = {0};
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->prefilter_passes[0],
                                       &attach_count, &attach_info);

    TbEnvShaderArgs args = {
        .rnd_sys = rnd_sys,
        .layout = sys.prefilter_pipe_layout,
        .color_format =
            tb_render_target_get_format(rt_sys, attach_info.attachment),
    };
    sys.prefilter_shader = tb_shader_load(ecs, create_prefilter_pipeline, &args,
                                          sizeof(TbEnvShaderArgs));
  }

  // Register passes with the render system
  sys.sky_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(SkyDrawBatch),
                  .draw_fn = record_sky,
                  .pass_id = sky_pass_id,
              });
  sys.irradiance_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(IrradianceBatch),
                  .draw_fn = record_irradiance,
                  .pass_id = irr_pass_id,
              });
  for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
    sys.env_capture_ctxs[i] = tb_render_pipeline_register_draw_context(
        rp_sys, &(TbDrawContextDescriptor){
                    .batch_size = sizeof(SkyDrawBatch),
                    .draw_fn = record_env_capture,
                    .pass_id = rp_sys->env_cap_passes[i],
                });
    sys.prefilter_ctxs[i] = tb_render_pipeline_register_draw_context(
        rp_sys, &(TbDrawContextDescriptor){
                    .batch_size = sizeof(PrefilterBatch),
                    .draw_fn = record_env_filter,
                    .pass_id = rp_sys->prefilter_passes[i],
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
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .size = skydome_size,
        };
        tb_rnd_sys_create_gpu_buffer_tmp(rnd_sys, &create_info,
                                         "SkyDome Geom Buffer",
                                         &sys.sky_geom_gpu_buffer, 16, &ptr);
      }
      copy_skydome(ptr);
    }
  }

  return sys;
}

void destroy_sky_system(ecs_world_t *ecs, TbSkySystem *self) {
  TbRenderSystem *rnd_sys = self->rnd_sys;

  vmaDestroyBuffer(rnd_sys->vma_alloc, self->sky_geom_gpu_buffer.buffer,
                   self->sky_geom_gpu_buffer.alloc);

  tb_rnd_destroy_set_layout(rnd_sys, self->sky_set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->sky_pipe_layout);
  tb_rnd_destroy_set_layout(rnd_sys, self->irr_set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->irr_pipe_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->prefilter_pipe_layout);
  tb_rnd_destroy_sampler(rnd_sys, self->irradiance_sampler);
  tb_shader_destroy(ecs, self->sky_shader);
  tb_shader_destroy(ecs, self->env_shader);
  tb_shader_destroy(ecs, self->irradiance_shader);
  tb_shader_destroy(ecs, self->prefilter_shader);

  *self = (TbSkySystem){0};
}

void tb_sky_draw_tick(ecs_iter_t *it) {
  TB_TRACY_SCOPEC("Sky Draw", TracyCategoryColorCore);
  ecs_world_t *ecs = it->world;

  TbWorld *world = ecs_singleton_ensure(ecs, TbWorldRef)->world;

  tb_auto sky_sys = ecs_singleton_ensure(ecs, TbSkySystem);
  ecs_singleton_modified(ecs, TbSkySystem);

  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);
  ecs_singleton_modified(ecs, TbRenderSystem);
  tb_auto rp_sys = ecs_singleton_ensure(ecs, TbRenderPipelineSystem);
  ecs_singleton_modified(ecs, TbRenderPipelineSystem);

  // Early out if any shaders aren't compiled yet
  if (!tb_is_shader_ready(ecs, sky_sys->sky_shader) ||
      !tb_is_shader_ready(ecs, sky_sys->env_shader) ||
      !tb_is_shader_ready(ecs, sky_sys->irradiance_shader) ||
      !tb_is_shader_ready(ecs, sky_sys->prefilter_shader)) {
    return;
  }

#if TB_USE_DESC_BUFFER == 1
  // Reset descriptor buffers
  {
    tb_reset_descriptor_buffer(rnd_sys, &sky_sys->sky_desc_buffer);
    tb_reset_descriptor_buffer(rnd_sys, &sky_sys->irr_desc_buffer);
  }
#endif

  // Write descriptor sets for each sky
  tb_auto skys = ecs_field(it, TbSkyComponent, 0);
  tb_auto trans = ecs_field(it, TbTransformComponent, 1);
  for (int32_t sky_idx = 0; sky_idx < it->count; ++sky_idx) {
    tb_auto sky = &skys[sky_idx];
    tb_auto transform = &trans[sky_idx];

    float3 sun_dir = -tb_transform_get_forward(&transform->transform);

    VkResult err = VK_SUCCESS;

#if TB_USE_DESC_BUFFER == 0
    const uint32_t write_count = 2; // +1 for irradiance pass
    {
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

      tb_auto layouts = tb_alloc_nm_tp(sky_sys->tmp_alloc, write_count,
                                       VkDescriptorSetLayout);
      layouts[0] = sky_sys->sky_set_layout;
      layouts[1] = sky_sys->irr_set_layout;

      tb_rnd_frame_desc_pool_tick(rnd_sys, "sky", &create_info, layouts, NULL,
                                  sky_sys->pools.pools, write_count,
                                  write_count);
    }

    // Just upload and write all views for now, they tend to be important anyway
    tb_auto writes =
        tb_alloc_nm_tp(sky_sys->tmp_alloc, write_count, VkWriteDescriptorSet);
    tb_auto buffer_info =
        tb_alloc_tp(sky_sys->tmp_alloc, VkDescriptorBufferInfo);
#endif
    TbSkyData data = {
        .time = (float)world->time,
        .cirrus = sky->cirrus,
        .cumulus = sky->cumulus,
        .sun_dir = sun_dir,
    };

    // Write view data into the tmp buffer we know will wind up on the GPU
    uint64_t offset = 0;
    err = tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, sizeof(TbSkyData), 0x40, &data,
                                        &offset);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for sky");

    tb_auto env_map_view = tb_render_target_get_view(
        sky_sys->rt_sys, rnd_sys->frame_idx, sky_sys->rt_sys->env_cube);

#if TB_USE_DESC_BUFFER == 1
    tb_auto tmp_gpu_buf_addr = tb_rnd_get_gpu_tmp_addr(rnd_sys);
    TbDescriptor sky_desc = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .data.pUniformBuffer =
            &(VkDescriptorAddressInfoEXT){
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                .address = tmp_gpu_buf_addr + offset,
                .range = sizeof(TbSkyData),
            },
    };
    tb_write_desc_to_buffer(rnd_sys, &sky_sys->sky_desc_buffer, 0, &sky_desc);

    TbDescriptor irr_desc = {
        .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .data.pSampledImage =
            &(VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                .imageView = env_map_view,
            },
    };
    tb_write_desc_to_buffer(rnd_sys, &sky_sys->irr_desc_buffer, 0, &irr_desc);
#else
    *buffer_info = (VkDescriptorBufferInfo){
        .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
        .offset = offset,
        .range = sizeof(TbSkyData),
    };

    // Construct a write descriptor
    writes[0] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet =
            tb_rnd_frame_desc_pool_get_set(rnd_sys, sky_sys->pools.pools, 0),
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = buffer_info,
    };

    // Last write is for the irradiance pass
    writes[1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet =
            tb_rnd_frame_desc_pool_get_set(rnd_sys, sky_sys->pools.pools, 1),
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
#endif
  }

  ecs_iter_t cam_it = ecs_query_iter(ecs, sky_sys->camera_query);
  while (ecs_query_next(&cam_it)) {
    tb_auto cameras = ecs_field(&cam_it, TbCameraComponent, 0);
    tb_auto transforms = ecs_field(&cam_it, TbTransformComponent, 1);
    for (int32_t cam_idx = 0; cam_idx < cam_it.count; ++cam_idx) {
      tb_auto camera = &cameras[cam_idx];
      tb_auto transform = &transforms[cam_idx];

#if TB_USE_DESC_BUFFER == 1
      tb_auto view_addr = tb_view_sys_get_table_addr(ecs, camera->view_id);
      if (view_addr.address == 0) {
        continue;
      }
#else
      tb_auto view_set =
          tb_view_system_get_descriptor(sky_sys->view_sys, camera->view_id);
      // Skip camera if view set isn't ready
      if (view_set == VK_NULL_HANDLE) {
        continue;
      }
#endif

      const uint32_t width = camera->width;
      const uint32_t height = camera->height;

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
        tb_auto sky_batch = tb_alloc_tp(sky_sys->tmp_alloc, SkyDrawBatch);
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
#if TB_USE_DESC_BUFFER == 1
            .desc_binding = tb_desc_buff_get_binding(&sky_sys->sky_desc_buffer),
#else
            .sky_set = tb_rnd_frame_desc_pool_get_set(rnd_sys,
                                                      sky_sys->pools.pools, 0),
#endif
            .geom_buffer = sky_sys->sky_geom_gpu_buffer.buffer,
            .index_count = get_skydome_index_count(),
            .vertex_offset = get_skydome_vert_offset(),
        };
        tb_auto sky_draw_batch = tb_alloc_tp(sky_sys->tmp_alloc, TbDrawBatch);
        *sky_draw_batch = (TbDrawBatch){
            .layout = sky_sys->sky_pipe_layout,
            .pipeline = tb_shader_get_pipeline(ecs, sky_sys->sky_shader),
            .viewport = {0, height, width, -(float)height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .user_batch = sky_batch,
        };

        tb_auto env_draw_batches = tb_alloc_nm_tp(
            sky_sys->tmp_alloc, PREFILTER_PASS_COUNT, TbDrawBatch);

        // We need to capture the environment once per
        // pre-filtered reflection mip in order to
        // avoid sun intensity artifacts
        for (uint32_t env_idx = 0; env_idx < PREFILTER_PASS_COUNT; ++env_idx) {
          const float mip_dim = FILTERED_ENV_DIM * SDL_powf(0.5f, env_idx);
          env_draw_batches[env_idx] = (TbDrawBatch){
              .layout = sky_sys->sky_pipe_layout,
              .pipeline = tb_shader_get_pipeline(ecs, sky_sys->env_shader),
              .viewport = {0, mip_dim, mip_dim, -mip_dim, 0, 1},
              .scissor = {{0, 0}, {mip_dim, mip_dim}},
              .user_batch = sky_batch,
          };
        }

        // Generate the batch for the irradiance pass
        tb_auto irr_draw_batch = tb_alloc_tp(sky_sys->tmp_alloc, TbDrawBatch);
        tb_auto irradiance_batch =
            tb_alloc_tp(sky_sys->tmp_alloc, IrradianceBatch);
        {
          *irr_draw_batch = (TbDrawBatch){
              .layout = sky_sys->irr_pipe_layout,
              .pipeline =
                  tb_shader_get_pipeline(ecs, sky_sys->irradiance_shader),
              .viewport = {0, 64, 64, -64, 0, 1},
              .scissor = {{0, 0}, {64, 64}},
              .user_batch = irradiance_batch,
          };
          *irradiance_batch = (IrradianceBatch){
#if TB_USE_DESC_BUFFER == 1
              .desc_binding =
                  tb_desc_buff_get_binding(&sky_sys->irr_desc_buffer),
#else
              .set = tb_rnd_frame_desc_pool_get_set(rnd_sys,
                                                    sky_sys->pools.pools, 1),
#endif
              .geom_buffer = sky_sys->sky_geom_gpu_buffer.buffer,
              .index_count = get_skydome_index_count(),
              .vertex_offset = get_skydome_vert_offset(),
          };
        }

        // Generate batch for prefiltering the environment map
        tb_auto pre_draw_batches = tb_alloc_nm_tp(
            sky_sys->tmp_alloc, PREFILTER_PASS_COUNT, TbDrawBatch);
        tb_auto prefilter_batches = tb_alloc_nm_tp(
            sky_sys->tmp_alloc, PREFILTER_PASS_COUNT, PrefilterBatch);
        {
          for (uint32_t i = 0; i < PREFILTER_PASS_COUNT; ++i) {
            const float mip_dim = FILTERED_ENV_DIM * SDL_powf(0.5f, i);

            pre_draw_batches[i] = (TbDrawBatch){
                .layout = sky_sys->prefilter_pipe_layout,
                .pipeline =
                    tb_shader_get_pipeline(ecs, sky_sys->prefilter_shader),
                .viewport = {0, mip_dim, mip_dim, -mip_dim, 0, 1},
                .scissor = {{0, 0}, {mip_dim, mip_dim}},
                .user_batch = &prefilter_batches[i],
            };

            prefilter_batches[i] = (PrefilterBatch){
#if TB_USE_DESC_BUFFER == 1
                .desc_binding =
                    tb_desc_buff_get_binding(&sky_sys->irr_desc_buffer),
#else
                .set = tb_rnd_frame_desc_pool_get_set(rnd_sys,
                                                      sky_sys->pools.pools, 1),
#endif
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
}

void tb_register_sky_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Sky Sys");
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbSkySystem);
  ECS_TAG_DEFINE(ecs, TbSkyRenderDirty);

  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);
  tb_auto rp_sys = ecs_singleton_ensure(ecs, TbRenderPipelineSystem);
  tb_auto rt_sys = ecs_singleton_ensure(ecs, TbRenderTargetSystem);
  tb_auto view_sys = ecs_singleton_ensure(ecs, TbViewSystem);

  TbSkySystem sys = create_sky_system(ecs, world->gp_alloc, world->tmp_alloc,
                                      rnd_sys, rp_sys, rt_sys, view_sys);
  sys.camera_query =
      ecs_query(ecs, {
                         .terms =
                             {
                                 {.id = ecs_id(TbCameraComponent)},
                                 {.id = ecs_id(TbTransformComponent)},
                             },
                         .cache_kind = EcsQueryCacheAuto,
                     });

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbSkySystem), TbSkySystem, &sys);

  ECS_SYSTEM(ecs, tb_sky_draw_tick,
             EcsOnStore, [inout] TbSkyComponent, [in] TbTransformComponent);
}

void tb_unregister_sky_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  tb_auto sys = ecs_singleton_ensure(ecs, TbSkySystem);
  ecs_query_fini(sys->camera_query);
  destroy_sky_system(ecs, sys);
  ecs_singleton_remove(ecs, TbSkySystem);
}
