#include "pipelines.h"

#include "color_mesh_frag.h"
#include "color_mesh_vert.h"
#include "fractal_frag.h"
#include "fractal_vert.h"
#include "gltf_frag.h"
#include "gltf_vert.h"
#include "gpuresources.h"
#include "imgui_frag.h"
#include "imgui_vert.h"
#include "shadercommon.h"
#include "sky_frag.h"
#include "sky_vert.h"
#include "uv_mesh_frag.h"
#include "uv_mesh_vert.h"

#include "gltf_closehit.h"
#include "gltf_miss.h"
#include "gltf_raygen.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vkdbg.h"
#include <volk.h>

uint32_t create_fractal_pipeline(VkDevice device,
                                 const VkAllocationCallbacks *vk_alloc,
                                 VkPipelineCache cache, VkRenderPass pass,
                                 uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;

  // Create Fullscreen Graphics Pipeline
  VkPipeline fractal_pipeline = VK_NULL_HANDLE;
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {0};
      create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      create_info.codeSize = sizeof(fractal_vert);
      create_info.pCode = (const uint32_t *)fractal_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      assert(err == VK_SUCCESS);

      create_info.codeSize = sizeof(fractal_frag);
      create_info.pCode = (const uint32_t *)fractal_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      assert(err == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_mod;
    vert_stage.pName = "vert";
    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_mod;
    frag_stage.pName = "frag";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
    vert_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, h, w, -(float)h, 0, 1};
    VkRect2D scissor = {{0, 0}, {w, h}};

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster_state = {0};
    raster_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {0};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    depth_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment_state = {0};
    attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &attachment_state;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        sizeof(dyn_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount =
        sizeof(shader_stages) / sizeof(VkPipelineShaderStageCreateInfo);
    create_info.pStages = shader_stages;
    create_info.pVertexInputState = &vert_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pDepthStencilState = &depth_state;
    create_info.pColorBlendState = &color_blend_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = pass;
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, vk_alloc,
                                    &fractal_pipeline);
    assert(err == VK_SUCCESS);

    // Can destroy shaders
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  *pipe = fractal_pipeline;

  return err;
}

uint32_t create_color_mesh_pipeline(VkDevice device,
                                    const VkAllocationCallbacks *vk_alloc,
                                    VkPipelineCache cache, VkRenderPass pass,
                                    uint32_t w, uint32_t h,
                                    VkPipelineLayout layout, VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;

  // Create Color Mesh Pipeline
  VkPipeline color_mesh_pipeline = VK_NULL_HANDLE;
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {0};
      create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      create_info.codeSize = sizeof(color_mesh_vert);
      create_info.pCode = (const uint32_t *)color_mesh_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      assert(err == VK_SUCCESS);

      create_info.codeSize = sizeof(color_mesh_frag);
      create_info.pCode = (const uint32_t *)color_mesh_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      assert(err == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_mod;
    vert_stage.pName = "vert";
    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_mod;
    frag_stage.pName = "frag";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    VkVertexInputBindingDescription vert_bindings[3] = {
        {0, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
        {2, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
    };

    VkVertexInputAttributeDescription vert_attrs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {2, 2, VK_FORMAT_R32G32B32_SFLOAT, 0},
    };

    VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
    vert_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vert_input_state.vertexBindingDescriptionCount = 3;
    vert_input_state.pVertexBindingDescriptions = vert_bindings;
    vert_input_state.vertexAttributeDescriptionCount = 3;
    vert_input_state.pVertexAttributeDescriptions = vert_attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, h, w, -(float)h, 0, 1};
    VkRect2D scissor = {{0, 0}, {w, h}};

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster_state = {0};
    raster_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {0};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    depth_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = VK_TRUE;
    depth_state.depthWriteEnable = VK_TRUE;
    depth_state.depthCompareOp = VK_COMPARE_OP_GREATER;
    depth_state.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment_state = {0};
    attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &attachment_state;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        sizeof(dyn_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount =
        sizeof(shader_stages) / sizeof(VkPipelineShaderStageCreateInfo);
    create_info.pStages = shader_stages;
    create_info.pVertexInputState = &vert_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pDepthStencilState = &depth_state;
    create_info.pColorBlendState = &color_blend_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = pass;
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, vk_alloc,
                                    &color_mesh_pipeline);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)color_mesh_pipeline, VK_OBJECT_TYPE_PIPELINE,
                "color mesh pipeline");

    // Can destroy shaders
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  *pipe = color_mesh_pipeline;
  return err;
}

uint32_t create_uv_mesh_pipeline(VkDevice device,
                                 const VkAllocationCallbacks *vk_alloc,
                                 VkPipelineCache cache, VkRenderPass pass,
                                 uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;

  // Create UV Mesh Pipeline
  VkPipeline uv_mesh_pipeline = VK_NULL_HANDLE;
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {0};
      create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      create_info.codeSize = sizeof(uv_mesh_vert);
      create_info.pCode = (const uint32_t *)uv_mesh_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      assert(err == VK_SUCCESS);

      create_info.codeSize = sizeof(uv_mesh_frag);
      create_info.pCode = (const uint32_t *)uv_mesh_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      assert(err == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_mod;
    vert_stage.pName = "vert";
    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_mod;
    frag_stage.pName = "frag";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    VkVertexInputBindingDescription vert_bindings[] = {
        {0, sizeof(float) * 8, VK_VERTEX_INPUT_RATE_VERTEX},
    };

    VkVertexInputAttributeDescription vert_attrs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6},
    };

    VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
    vert_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vert_input_state.vertexBindingDescriptionCount = 1;
    vert_input_state.pVertexBindingDescriptions = vert_bindings;
    vert_input_state.vertexAttributeDescriptionCount = 3;
    vert_input_state.pVertexAttributeDescriptions = vert_attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, h, w, -(float)h, 0, 1};
    VkRect2D scissor = {{0, 0}, {w, h}};

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster_state = {0};
    raster_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {0};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    depth_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = VK_TRUE;
    depth_state.depthWriteEnable = VK_TRUE;
    depth_state.depthCompareOp = VK_COMPARE_OP_GREATER;
    depth_state.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment_state = {0};
    attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &attachment_state;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        sizeof(dyn_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount =
        sizeof(shader_stages) / sizeof(VkPipelineShaderStageCreateInfo);
    create_info.pStages = shader_stages;
    create_info.pVertexInputState = &vert_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pDepthStencilState = &depth_state;
    create_info.pColorBlendState = &color_blend_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = pass;
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, vk_alloc,
                                    &uv_mesh_pipeline);
    assert(err == VK_SUCCESS);

    // Can destroy shaders
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  *pipe = uv_mesh_pipeline;
  return err;
}

uint32_t create_skydome_pipeline(VkDevice device,
                                 const VkAllocationCallbacks *vk_alloc,
                                 VkPipelineCache cache, VkRenderPass pass,
                                 uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;

  // Create Skydome Pipeline
  VkPipeline pipeline = VK_NULL_HANDLE;
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {0};
      create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      create_info.codeSize = sizeof(sky_vert);
      create_info.pCode = (const uint32_t *)sky_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      assert(err == VK_SUCCESS);

      create_info.codeSize = sizeof(sky_frag);
      create_info.pCode = (const uint32_t *)sky_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      assert(err == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_mod;
    vert_stage.pName = "vert";
    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_mod;
    frag_stage.pName = "frag";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    VkVertexInputBindingDescription vert_bindings[1] = {
        {0, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
    };

    VkVertexInputAttributeDescription vert_attrs[1] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
    };

    VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
    vert_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vert_input_state.vertexBindingDescriptionCount = 1;
    vert_input_state.pVertexBindingDescriptions = vert_bindings;
    vert_input_state.vertexAttributeDescriptionCount = 1;
    vert_input_state.pVertexAttributeDescriptions = vert_attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, h, w, -(float)h, 0, 1};
    VkRect2D scissor = {{0, 0}, {w, h}};

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster_state = {0};
    raster_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_FRONT_BIT;
    raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {0};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    depth_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = VK_TRUE;
    depth_state.depthWriteEnable = VK_FALSE;
    depth_state.depthCompareOp = VK_COMPARE_OP_EQUAL; // Equal to 0
    depth_state.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment_state = {0};
    attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &attachment_state;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        sizeof(dyn_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount =
        sizeof(shader_stages) / sizeof(VkPipelineShaderStageCreateInfo);
    create_info.pStages = shader_stages;
    create_info.pVertexInputState = &vert_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pDepthStencilState = &depth_state;
    create_info.pColorBlendState = &color_blend_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = pass;
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, vk_alloc,
                                    &pipeline);
    assert(err == VK_SUCCESS);

    set_vk_name(device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE,
                "skydome pipeline");

    // Can destroy shaders
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  *pipe = pipeline;
  return err;
}

uint32_t create_imgui_pipeline(VkDevice device,
                               const VkAllocationCallbacks *vk_alloc,
                               VkPipelineCache cache, VkRenderPass pass,
                               uint32_t w, uint32_t h, VkPipelineLayout layout,
                               VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;

  // Create ImGui Pipeline
  VkPipeline pipeline = VK_NULL_HANDLE;
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {0};
      create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      create_info.codeSize = sizeof(imgui_vert);
      create_info.pCode = (const uint32_t *)imgui_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      assert(err == VK_SUCCESS);

      create_info.codeSize = sizeof(imgui_frag);
      create_info.pCode = (const uint32_t *)imgui_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      assert(err == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_mod;
    vert_stage.pName = "vert";
    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_mod;
    frag_stage.pName = "frag";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    VkVertexInputBindingDescription vert_bindings[1] = {
        {0, sizeof(float2) + sizeof(float2) + sizeof(uint32_t),
         VK_VERTEX_INPUT_RATE_VERTEX},
    };

    VkVertexInputAttributeDescription vert_attrs[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float2)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float2) * 2},
    };

    VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
    vert_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vert_input_state.vertexBindingDescriptionCount = 1;
    vert_input_state.pVertexBindingDescriptions = vert_bindings;
    vert_input_state.vertexAttributeDescriptionCount = 3;
    vert_input_state.pVertexAttributeDescriptions = vert_attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, h, w, -(float)h, 0, 1};
    VkRect2D scissor = {{0, 0}, {w, h}};

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster_state = {0};
    raster_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_NONE;
    raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {0};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState attachment_state = {0};
    attachment_state.blendEnable = VK_TRUE;
    attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &attachment_state;

    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    depth_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        sizeof(dyn_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount =
        sizeof(shader_stages) / sizeof(VkPipelineShaderStageCreateInfo);
    create_info.pStages = shader_stages;
    create_info.pVertexInputState = &vert_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pColorBlendState = &color_blend_state;
    create_info.pDepthStencilState = &depth_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = pass;
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, vk_alloc,
                                    &pipeline);
    assert(err == VK_SUCCESS);

    // Can destroy shaders
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  *pipe = pipeline;
  return err;
}

uint32_t create_gltf_pipeline(VkDevice device,
                              const VkAllocationCallbacks *vk_alloc,
                              Allocator tmp_alloc, Allocator std_alloc,
                              VkPipelineCache cache, VkRenderPass pass,
                              uint32_t w, uint32_t h, VkPipelineLayout layout,
                              GPUPipeline **pipe) {
  VkResult err = VK_SUCCESS;

  VkVertexInputBindingDescription vert_bindings[3] = {
      {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {2, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputAttributeDescription vert_attrs[3] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {2, 2, VK_FORMAT_R32G32_SFLOAT, 0},
  };

  VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
  vert_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vert_input_state.vertexBindingDescriptionCount = 3;
  vert_input_state.pVertexBindingDescriptions = vert_bindings;
  vert_input_state.vertexAttributeDescriptionCount = 3;
  vert_input_state.pVertexAttributeDescriptions = vert_attrs;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
  input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport viewport = {0, h, w, -(float)h, 0, 1};
  VkRect2D scissor = {{0, 0}, {w, h}};

  VkPipelineViewportStateCreateInfo viewport_state = {0};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;
  VkPipelineRasterizationStateCreateInfo raster_state = {0};
  raster_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster_state.polygonMode = VK_POLYGON_MODE_FILL;
  raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
  raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster_state.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample_state = {0};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth_state = {0};
  depth_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_state.depthTestEnable = VK_TRUE;
  depth_state.depthWriteEnable = VK_TRUE;
  depth_state.depthCompareOp = VK_COMPARE_OP_GREATER;
  depth_state.maxDepthBounds = 1.0f;

  VkPipelineColorBlendAttachmentState attachment_state = {0};
  attachment_state.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
  color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.attachmentCount = 1;
  color_blend_state.pAttachments = &attachment_state;

  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {0};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = sizeof(dyn_states) / sizeof(VkDynamicState);
  dynamic_state.pDynamicStates = dyn_states;

  // Load Shader Modules
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  VkShaderModuleCreateInfo shader_mod_create_info = {0};
  shader_mod_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_mod_create_info.codeSize = sizeof(gltf_vert);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_vert;
  err = vkCreateShaderModule(device, &shader_mod_create_info, vk_alloc,
                             &vert_mod);
  assert(err == VK_SUCCESS);

  shader_mod_create_info.codeSize = sizeof(gltf_frag);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_frag;
  err = vkCreateShaderModule(device, &shader_mod_create_info, vk_alloc,
                             &frag_mod);
  assert(err == VK_SUCCESS);

  VkPipelineShaderStageCreateInfo vert_stage = {0};
  vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_stage.module = vert_mod;
  vert_stage.pName = "vert";

  VkPipelineShaderStageCreateInfo frag_stage = {0};
  frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_stage.module = frag_mod;
  frag_stage.pName = "frag";

  VkPipelineShaderStageCreateInfo stages[2] = {vert_stage, frag_stage};

  VkGraphicsPipelineCreateInfo create_info_base = {0};
  create_info_base.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  create_info_base.stageCount = 2;
  create_info_base.pStages = stages;
  create_info_base.pVertexInputState = &vert_input_state;
  create_info_base.pInputAssemblyState = &input_assembly_state;
  create_info_base.pViewportState = &viewport_state;
  create_info_base.pRasterizationState = &raster_state;
  create_info_base.pMultisampleState = &multisample_state;
  create_info_base.pDepthStencilState = &depth_state;
  create_info_base.pColorBlendState = &color_blend_state;
  create_info_base.pDynamicState = &dynamic_state;
  create_info_base.layout = layout;
  create_info_base.renderPass = pass;

  // Calculate number of permuatations
  uint32_t perm_count = 1 << GLTF_PERM_FLAG_COUNT;

  GPUPipeline *p = NULL;

  err = (VkResult)create_gfx_pipeline(device, vk_alloc, tmp_alloc, std_alloc,
                                      cache, perm_count, &create_info_base, &p);
  assert(err == VK_SUCCESS);

  // Can destroy shader moduless
  vkDestroyShaderModule(device, vert_mod, vk_alloc);
  vkDestroyShaderModule(device, frag_mod, vk_alloc);

  *pipe = p;

  return err;
}

uint32_t create_gltf_rt_pipeline(
    VkDevice device, const VkAllocationCallbacks *vk_alloc, Allocator tmp_alloc,
    Allocator std_alloc, VkPipelineCache cache,
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelines,
    VkPipelineLayout layout, GPUPipeline **pipe) {
  VkResult err = VK_SUCCESS;

  // Load shaders and setup groups
  // Ray Gen
  VkShaderModule raygen_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_raygen),
        .pCode = (const uint32_t *)gltf_raygen,
    };
    err = vkCreateShaderModule(device, &create_info, vk_alloc, &raygen_mod);
    assert(err == VK_SUCCESS);
  }

  // Miss
  VkShaderModule miss_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_miss),
        .pCode = (const uint32_t *)gltf_miss,
    };
    err = vkCreateShaderModule(device, &create_info, vk_alloc, &miss_mod);
    assert(err == VK_SUCCESS);
  }

  // Closest Hit
  VkShaderModule closehit_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_closehit),
        .pCode = (const uint32_t *)gltf_closehit,
    };
    err = vkCreateShaderModule(device, &create_info, vk_alloc, &closehit_mod);
    assert(err == VK_SUCCESS);
  }

  VkPipelineShaderStageCreateInfo shader_stages[3] = {
      [0] =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
              .module = raygen_mod,
              .pName = "raygen"},
      [1] =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
              .module = miss_mod,
              .pName = "miss"},
      [2] =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
              .module = closehit_mod,
              .pName = "closehit"},

  };
  VkRayTracingShaderGroupCreateInfoKHR shader_group_info[3] = {
      [0] =
          (VkRayTracingShaderGroupCreateInfoKHR){
              .sType =
                  VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
              .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
              .generalShader = 0,
              .closestHitShader = VK_SHADER_UNUSED_KHR,
              .anyHitShader = VK_SHADER_UNUSED_KHR,
              .intersectionShader = VK_SHADER_UNUSED_KHR,
          },
      [1] =
          (VkRayTracingShaderGroupCreateInfoKHR){
              .sType =
                  VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
              .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
              .generalShader = 1,
              .closestHitShader = VK_SHADER_UNUSED_KHR,
              .anyHitShader = VK_SHADER_UNUSED_KHR,
              .intersectionShader = VK_SHADER_UNUSED_KHR,
          },
      [2] =
          (VkRayTracingShaderGroupCreateInfoKHR){
              .sType =
                  VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
              .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
              .generalShader = VK_SHADER_UNUSED_KHR,
              .closestHitShader = 2,
              .anyHitShader = VK_SHADER_UNUSED_KHR,
              .intersectionShader = VK_SHADER_UNUSED_KHR,
          },
  };

  // Create Pipeline
  VkRayTracingPipelineCreateInfoKHR create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
  create_info.stageCount = 3;
  create_info.pStages = shader_stages;
  create_info.groupCount = 3;
  create_info.pGroups = shader_group_info;
  create_info.maxPipelineRayRecursionDepth = 1;
  create_info.layout = layout;

  err = (VkResult)create_rt_pipeline(device, vk_alloc, tmp_alloc, std_alloc,
                                     cache, vkCreateRayTracingPipelines, 1,
                                     &create_info, pipe);
  assert(err == VK_SUCCESS);

  // Cleanup modules
  vkDestroyShaderModule(device, raygen_mod, vk_alloc);
  vkDestroyShaderModule(device, miss_mod, vk_alloc);
  vkDestroyShaderModule(device, closehit_mod, vk_alloc);

  return (uint32_t)err;
}
