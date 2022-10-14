#include "skysystem.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "irradiance_frag.h"
#include "irradiance_vert.h"
#include "sky_cube_frag.h"
#include "sky_cube_vert.h"
#include "sky_frag.h"
#include "sky_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "cameracomponent.h"
#include "common.hlsli"
#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "sky.hlsli"
#include "skycomponent.h"
#include "skydome.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

typedef struct SkyDrawBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;

  VkViewport viewport;
  VkRect2D scissor;

  VkPushConstantRange const_range;
  SkyPushConstants consts;
  VkDescriptorSet sky_set;

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} SkyDrawBatch;

typedef struct IrradianceBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;

  VkViewport viewport;
  VkRect2D scissor;

  VkDescriptorSet set;

  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} IrradianceBatch;

VkResult create_sky_pipeline2(RenderSystem *render_system, VkRenderPass pass,
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

  const uint32_t stage_count = 2;
  VkPipelineShaderStageCreateInfo stages[stage_count] = {
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
  };

  VkVertexInputBindingDescription vert_bindings[1] = {
      {0, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
  };
  VkVertexInputAttributeDescription vert_attrs[1] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
  };
  VkPipelineVertexInputStateCreateInfo vert_input_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = vert_bindings,
      .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = vert_attrs,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  VkViewport viewport = {0, 600.0f, 800.0f, -600.0f, 0, 1};
  VkRect2D scissor = {{0, 0}, {800, 600}};
  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor,
  };

  VkPipelineRasterizationStateCreateInfo raster_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineColorBlendAttachmentState attachment_state = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &attachment_state,
  };

  VkPipelineDepthStencilStateCreateInfo depth_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_EQUAL,
      .maxDepthBounds = 1.0f,
  };

  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dyn_states,
  };

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = stage_count,
      .pStages = stages,
      .pVertexInputState = &vert_input_state,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster_state,
      .pMultisampleState = &multisample_state,
      .pColorBlendState = &color_blend_state,
      .pDepthStencilState = &depth_state,
      .pDynamicState = &dynamic_state,
      .layout = layout,
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Sky Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create sky pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_env_capture_pipeline(RenderSystem *render_system,
                                     VkRenderPass pass, VkPipelineLayout layout,
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
      .layout = layout,
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Env Capture Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to env capture pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_irradiance_pipeline(RenderSystem *render_system,
                                    VkRenderPass pass, VkPipelineLayout layout,
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
      .layout = layout,
      .renderPass = pass,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Irradiance Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create irradiance pipeline", err);

  // Can safely dispose of shader module objects
  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

void record_sky_common(VkCommandBuffer buffer, uint32_t batch_count,
                       const SkyDrawBatch *batches) {
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const SkyDrawBatch *batch = &batches[batch_idx];
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    VkPushConstantRange range = batch->const_range;
    const SkyPushConstants *consts = &batch->consts;
    vkCmdPushConstants(buffer, batch->layout, range.stageFlags, range.offset,
                       range.size, consts);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &batch->sky_set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, batch->geom_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &batch->geom_buffer,
                           &batch->vertex_offset);
    vkCmdDrawIndexed(buffer, batch->index_count, 1, 0, 0, 0);
  }
}

void record_sky(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Sky Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Sky", 1, true);
  cmd_begin_label(buffer, "Sky", (float4){0.8f, 0.8f, 0.0f, 1.0f});

  record_sky_common(buffer, batch_count, (const SkyDrawBatch *)batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_env_capture(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Env Capture Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Env Capture", 1, true);
  cmd_begin_label(buffer, "Env Capture", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  record_sky_common(buffer, batch_count, (const SkyDrawBatch *)batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void record_irradiance(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const void *batches) {

  TracyCZoneNC(ctx, "Irradiance Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Irradiance", 1, true);
  cmd_begin_label(buffer, "Irradiance", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  if (batch_count > 0) {
    const IrradianceBatch *batch = (const IrradianceBatch *)batches;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &batch->set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, batch->geom_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &batch->geom_buffer,
                           &batch->vertex_offset);
    vkCmdDrawIndexed(buffer, batch->index_count, 1, 0, 0, 0);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

bool create_sky_system(SkySystem *self, const SkySystemDescriptor *desc,
                       uint32_t system_dep_count, System *const *system_deps) {
  // Find necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which sky depends on",
                  VK_ERROR_UNKNOWN);
  RenderPipelineSystem *render_pipe_system =
      tb_get_system(system_deps, system_dep_count, RenderPipelineSystem);
  TB_CHECK_RETURN(render_pipe_system,
                  "Failed to find render pipeline system which sky depends on",
                  false);
  RenderTargetSystem *render_target_system =
      tb_get_system(system_deps, system_dep_count, RenderTargetSystem);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find render target system which sky depends on",
                  false);

  *self = (SkySystem){
      .render_system = render_system,
      .render_pipe_system = render_pipe_system,
      .render_target_system = render_target_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Get passes
  TbRenderPassId sky_pass_id = self->render_pipe_system->sky_pass;
  TbRenderPassId env_cap_id = self->render_pipe_system->env_capture_pass;
  TbRenderPassId irr_pass_id = self->render_pipe_system->irradiance_pass;
  self->sky_pass =
      tb_render_pipeline_get_pass(self->render_pipe_system, sky_pass_id);
  self->env_capture_pass =
      tb_render_pipeline_get_pass(self->render_pipe_system, env_cap_id);
  self->irradiance_pass =
      tb_render_pipeline_get_pass(self->render_pipe_system, irr_pass_id);

  // Create immutable sampler
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
                                "Irradiance Sampler", &self->sampler);
    TB_VK_CHECK_RET(err, "Failed to create irradiance sampler", false);
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
                                   "Sky Set Layout", &self->sky_set_layout);
    TB_VK_CHECK_RET(err, "Failed to create sky descriptor set layout", false);
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
                 &self->sampler},
            },
    };
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "Irradiance Set Layout",
                                   &self->irr_set_layout);
    TB_VK_CHECK_RET(err, "Failed to irradiance sky descriptor set layout",
                    false);
  }

  // Create Pipeline Layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &self->sky_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            &(VkPushConstantRange){
                VK_SHADER_STAGE_ALL_GRAPHICS,
                0,
                sizeof(SkyPushConstants),
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Sky Pipeline Layout",
                                        &self->sky_pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create sky pipeline layout", false);
  }
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &self->irr_set_layout,
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Irradiance Pipeline Layout",
                                        &self->irr_pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create irradiance pipeline layout", false);
  }

  // Create sky pipeline
  err = create_sky_pipeline2(render_system, self->sky_pass,
                             self->sky_pipe_layout, &self->sky_pipeline);
  TB_VK_CHECK_RET(err, "Failed to create sky pipeline", false);

  // Create env capture pipeline
  err = create_env_capture_pipeline(render_system, self->env_capture_pass,
                                    self->sky_pipe_layout, &self->env_pipeline);
  TB_VK_CHECK_RET(err, "Failed to create env capture pipeline", false);

  // Create irradiance pipeline
  err = create_irradiance_pipeline(render_system, self->irradiance_pass,
                                   self->irr_pipe_layout,
                                   &self->irradiance_pipeline);
  TB_VK_CHECK_RET(err, "Failed to create irradiance pipeline", false);

  // Register passes with the render system
  self->sky_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(SkyDrawBatch),
                              .draw_fn = record_sky,
                              .pass_id = sky_pass_id,
                          });
  self->env_capture_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(SkyDrawBatch),
                              .draw_fn = record_env_capture,
                              .pass_id = env_cap_id,
                          });
  self->irradiance_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(IrradianceBatch),
                              .draw_fn = record_irradiance,
                              .pass_id = irr_pass_id,
                          });

  // Create sky box geometry
  {
    const uint64_t skydome_size = get_skydome_size();
    // Make space for the sky geometry on the GPU
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          .size = skydome_size,
      };
      err = tb_rnd_sys_alloc_gpu_buffer(render_system, &create_info,
                                        "Sky Geom Buffer",
                                        &self->sky_geom_gpu_buffer);
      TB_VK_CHECK_RET(err, "Failed to alloc imgui atlas", false);
    }

    // Use the gpu tmp buffer to copy the geom buffer
    {
      TbHostBuffer host_buf = {0};
      err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, skydome_size, 16,
                                             &host_buf);
      TB_VK_CHECK_RET(err, "Failed to alloc tmp space for the skydome geometry",
                      false);
      copy_skydome(host_buf.ptr); // Copy to the newly alloced host buffer

      {
        BufferCopy skydome_copy = {
            .src = host_buf.buffer,
            .dst = self->sky_geom_gpu_buffer.buffer,
            .region =
                {
                    .srcOffset = host_buf.offset,
                    .size = skydome_size,
                },
        };
        tb_rnd_upload_buffers(render_system, &skydome_copy, 1);
      }
    }
  }

  return true;
}

void destroy_sky_system(SkySystem *self) {
  RenderSystem *render_system = self->render_system;

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
  tb_rnd_destroy_sampler(render_system, self->sampler);
  tb_rnd_destroy_pipeline(render_system, self->sky_pipeline);
  tb_rnd_destroy_pipeline(render_system, self->env_pipeline);
  tb_rnd_destroy_pipeline(render_system, self->irradiance_pipeline);

  *self = (SkySystem){0};
}

void tick_sky_system(SkySystem *self, const SystemInput *input,
                     SystemOutput *output, float delta_seconds) {
  EntityId *entities = tb_get_column_entity_ids(input, 0);

  const PackedComponentStore *skys =
      tb_get_column_check_id(input, 0, 0, SkyComponentId);
  const uint32_t sky_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *cameras =
      tb_get_column_check_id(input, 1, 0, CameraComponentId);
  const PackedComponentStore *transforms =
      tb_get_column_check_id(input, 1, 1, TransformComponentId);
  const uint32_t camera_count = tb_get_column_component_count(input, 1);

  if (skys == NULL || cameras == NULL || transforms == NULL) {
    return;
  }

  // TODO: Make this less hacky
  const uint32_t width = self->render_system->render_thread->swapchain.width;
  const uint32_t height = self->render_system->render_thread->swapchain.height;

  if (camera_count > 0 && sky_count > 0) {
    const CameraComponent *camera_comps =
        (const CameraComponent *)cameras->components;
    const TransformComponent *transform_comps =
        (const TransformComponent *)transforms->components;
    const SkyComponent *sky_comps = (const SkyComponent *)skys->components;

    VkResult err = VK_SUCCESS;
    RenderSystem *render_system = self->render_system;
    VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);

    // Copy the sky component for output
    SkyComponent *out_skys =
        tb_alloc_nm_tp(self->tmp_alloc, sky_count, SkyComponent);
    SDL_memcpy(out_skys, sky_comps, sky_count * sizeof(SkyComponent));

    const uint32_t write_count = sky_count + 1; // +1 for irradiance pass

    SkySystemFrameState *state = &self->frame_states[render_system->frame_idx];
    // Allocate all the descriptor sets for this frame
    {
      // Resize the pool
      if (state->set_count < write_count) {
        if (state->set_pool) {
          tb_rnd_destroy_descriptor_pool(render_system, state->set_pool);
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
            render_system, &create_info,
            "Sky System Frame State Descriptor Pool", &state->set_pool);
        TB_VK_CHECK(err,
                    "Failed to create sky system frame state descriptor pool");
      } else {
        vkResetDescriptorPool(self->render_system->render_thread->device,
                              state->set_pool, 0);
      }
      state->set_count = write_count;
      state->sets = tb_realloc_nm_tp(self->std_alloc, state->sets,
                                     state->set_count, VkDescriptorSet);

      VkDescriptorSetLayout *layouts =
          tb_alloc_nm_tp(self->tmp_alloc, write_count, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < state->set_count; ++i) {
        layouts[i] = self->sky_set_layout;
      }
      layouts[sky_count] = self->irr_set_layout;

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = state->set_count,
          .descriptorPool = state->set_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(render_system->render_thread->device,
                                     &alloc_info, state->sets);
      TB_VK_CHECK(err, "Failed to re-allocate sky descriptor sets");
    }

    // Just upload and write all views for now, they tend to be important anyway
    VkWriteDescriptorSet *writes =
        tb_alloc_nm_tp(self->tmp_alloc, write_count, VkWriteDescriptorSet);
    VkDescriptorBufferInfo *buffer_info =
        tb_alloc_nm_tp(self->tmp_alloc, sky_count, VkDescriptorBufferInfo);
    TbHostBuffer *buffers =
        tb_alloc_nm_tp(self->tmp_alloc, sky_count, TbHostBuffer);
    for (uint32_t sky_idx = 0; sky_idx < sky_count; ++sky_idx) {
      SkyComponent *comp = &out_skys[sky_idx];
      comp->time += delta_seconds;
      SkyData data = {
          .time = comp->time,
          .cirrus = comp->cirrus,
          .cumulus = comp->cumulus,
          .sun_dir = comp->sun_dir,
      };
      TbHostBuffer *buffer = &buffers[sky_idx];

      // Write view data into the tmp buffer we know will wind up on the GPU
      err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, sizeof(SkyData),
                                             0x40, buffer);
      TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for sky");

      // Copy view data to the allocated buffer
      SDL_memcpy(buffer->ptr, &data, sizeof(SkyData));

      // Get the descriptor we want to write to
      VkDescriptorSet view_set = state->sets[sky_idx];

      buffer_info[sky_idx] = (VkDescriptorBufferInfo){
          .buffer = tmp_gpu_buffer,
          .offset = buffer->offset,
          .range = sizeof(SkyData),
      };

      // Construct a write descriptor
      writes[sky_idx] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = view_set,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo = &buffer_info[sky_idx],
      };
    }
    // Last write is for the irradiance pass
    VkImageView env_map_view = tb_render_target_get_view(
        self->render_target_system, self->render_system->frame_idx,
        self->render_target_system->env_cube);
    writes[sky_count] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = state->sets[sky_count],
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
    vkUpdateDescriptorSets(render_system->render_thread->device, write_count,
                           writes, 0, NULL);

    uint32_t batch_count = 0;
    const size_t batch_bytes = sky_count * camera_count;
    Allocator tmp_alloc = self->render_system->render_thread
                              ->frame_states[self->render_system->frame_idx]
                              .tmp_alloc.alloc;
    SkyDrawBatch *sky_batches =
        tb_alloc_nm_tp(tmp_alloc, batch_bytes, SkyDrawBatch);
    SkyDrawBatch *env_batches =
        tb_alloc_nm_tp(tmp_alloc, batch_bytes, SkyDrawBatch);

    // Submit a sky draw for each camera, for each sky
    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      const CameraComponent *camera = &camera_comps[cam_idx];
      const TransformComponent *transform = &transform_comps[cam_idx];

      // Need to manually calculate this here
      float4x4 vp = {.row0 = {0}};
      {
        float4x4 proj = {.row0 = {0}};
        perspective(&proj, camera->fov, camera->aspect_ratio, camera->near,
                    camera->far);

        float4x4 model = {.row0 = {0}};
        transform_to_matrix(&model, &transform->transform);
        float3 forward = f4tof3(model.row2);

        float4x4 view = {.row0 = {0}};
        look_forward(&view, (float3){0.0f, 0.0f, 0.0f}, forward,
                     (float3){0.0f, 1.0f, 0.0f});

        mulmf44(&proj, &view, &vp);
      }

      for (uint32_t sky_idx = 0; sky_idx < sky_count; ++sky_idx) {
        sky_batches[batch_count] = (SkyDrawBatch){
            .layout = self->sky_pipe_layout,
            .pipeline = self->sky_pipeline,
            .viewport = {0, 0, width, height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .const_range =
                (VkPushConstantRange){
                    .size = sizeof(SkyPushConstants),
                    .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                },
            .consts =
                {
                    .vp = vp,
                },
            .sky_set = state->sets[batch_count],
            .geom_buffer = self->sky_geom_gpu_buffer.buffer,
            .index_count = get_skydome_index_count(),
            .vertex_offset = get_skydome_vert_offset(),
        };
        env_batches[batch_count] = sky_batches[batch_count];
        env_batches[batch_count].pipeline = self->env_pipeline;
        env_batches[batch_count].viewport = (VkViewport){0, 0, 256, 256, 0, 1};
        env_batches[batch_count].scissor = (VkRect2D){{0, 0}, {256, 256}};
        batch_count++;
      }
    }

    // Generate the batch for the irradiance pass
    IrradianceBatch *irradiance_batch =
        tb_alloc_nm_tp(tmp_alloc, sizeof(IrradianceBatch), IrradianceBatch);
    {
      *irradiance_batch = (IrradianceBatch){
          .layout = self->irr_pipe_layout,
          .pipeline = self->irradiance_pipeline,
          .viewport = {0, 0, 64, 64, 0, 1},
          .scissor = {{0, 0}, {64, 64}},
          .set = state->sets[state->set_count - 1],
          .geom_buffer = self->sky_geom_gpu_buffer.buffer,
          .index_count = get_skydome_index_count(),
          .vertex_offset = get_skydome_vert_offset(),
      };
    }

    tb_render_pipeline_issue_draw_batch(
        self->render_pipe_system, self->sky_draw_ctx, batch_count, sky_batches);
    tb_render_pipeline_issue_draw_batch(self->render_pipe_system,
                                        self->env_capture_ctx, batch_count,
                                        env_batches);
    tb_render_pipeline_issue_draw_batch(
        self->render_pipe_system, self->irradiance_ctx, 1, irradiance_batch);

    // Report output (we've updated the time on the sky component)
    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .id = SkyComponentId,
        .count = sky_count,
        .components = (uint8_t *)out_skys,
        .entities = entities,
    };
  }
}

TB_DEFINE_SYSTEM(sky, SkySystem, SkySystemDescriptor)

void tb_sky_system_descriptor(SystemDescriptor *desc,
                              const SkySystemDescriptor *sky_desc) {
  *desc = (SystemDescriptor){
      .name = "Sky",
      .size = sizeof(SkySystem),
      .id = SkySystemId,
      .desc = (InternalDescriptor)sky_desc,
      .dep_count = 2,
      .deps[0] =
          {
              .count = 1,
              .dependent_ids = {SkyComponentId},
          },
      .deps[1] =
          {
              .count = 2,
              .dependent_ids = {CameraComponentId, TransformComponentId},
          },
      .system_dep_count = 3,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = RenderPipelineSystemId,
      .system_deps[2] = RenderTargetSystemId,
      .create = tb_create_sky_system,
      .destroy = tb_destroy_sky_system,
      .tick = tb_tick_sky_system,
  };
}
