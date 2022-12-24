#include "oceansystem.h"

#include "assets.h"
#include "cameracomponent.h"
#include "cgltf.h"
#include "lightcomponent.h"
#include "meshsystem.h"
#include "ocean.hlsli"
#include "oceancomponent.h"
#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "shadow.hlsli"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "ocean_frag.h"
#include "ocean_vert.h"
#include "oceanprepass_frag.h"
#include "oceanprepass_vert.h"
#include "oceanshadow_frag.h"
#include "oceanshadow_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

typedef struct OceanDrawBatch {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkViewport viewport;
  VkRect2D scissor;
  VkDescriptorSet view_set;
  VkDescriptorSet ocean_set;
  VkBuffer geom_buffer;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t pos_offset;
} OceanDrawBatch;

typedef struct OceanShadowBatch {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkViewport viewport;
  VkRect2D scissor;
  VkDescriptorSet ocean_set;
  ShadowViewConstants shadow_consts;
  VkBuffer geom_buffer;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t pos_offset;
} OceanShadowBatch;

void ocean_record(VkCommandBuffer buffer, uint32_t batch_count,
                  const OceanDrawBatch *batches) {
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const OceanDrawBatch *batch = &batches[batch_idx];
    VkPipelineLayout layout = batch->layout;
    VkBuffer geom_buffer = batch->geom_buffer;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            1, &batch->ocean_set, 0, NULL);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                            1, &batch->view_set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, geom_buffer, 0, batch->index_type);
    vkCmdBindVertexBuffers(buffer, 0, 1, &geom_buffer, &batch->pos_offset);

    vkCmdDrawIndexed(buffer, batch->index_count, 1, 0, 0, 0);
  }
}

void ocean_prepass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                          uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Ocean Prepass Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean Prepass", 1, true);
  cmd_begin_label(buffer, "Ocean Prepass", (float4){0.0f, 0.4f, 0.4f, 1.0f});

  const OceanDrawBatch *ocean_batches = (const OceanDrawBatch *)batches;
  ocean_record(buffer, batch_count, ocean_batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void ocean_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Ocean Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean", 1, true);
  cmd_begin_label(buffer, "Ocean", (float4){0.0f, 0.8f, 0.8f, 1.0f});

  const OceanDrawBatch *ocean_batches = (const OceanDrawBatch *)batches;
  ocean_record(buffer, batch_count, ocean_batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void ocean_shadow_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                         uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Ocean Shadow Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean Shadows", 1, true);
  cmd_begin_label(buffer, "Ocean Shadows", (float4){0.0f, 0.4f, 0.4f, 1.0f});

  const OceanShadowBatch *shadow_batches = (const OceanShadowBatch *)batches;
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const OceanShadowBatch *batch = &shadow_batches[batch_idx];
    VkPipelineLayout layout = batch->layout;
    VkBuffer geom_buffer = batch->geom_buffer;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            1, &batch->ocean_set, 0, NULL);
    vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(ShadowViewConstants), &batch->shadow_consts);

    vkCmdBindIndexBuffer(buffer, geom_buffer, 0, batch->index_type);
    vkCmdBindVertexBuffers(buffer, 0, 1, &geom_buffer, &batch->pos_offset);

    vkCmdDrawIndexed(buffer, batch->index_count, 1, 0, 0, 0);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_ocean_pipelines(RenderSystem *render_system,
                                VkRenderPass prepass, VkRenderPass pass,
                                VkPipelineLayout pipe_layout,
                                VkPipeline *prepass_pipeline,
                                VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule oceanprepass_vert_mod = VK_NULL_HANDLE;
  VkShaderModule oceanprepass_frag_mod = VK_NULL_HANDLE;

  VkShaderModule ocean_vert_mod = VK_NULL_HANDLE;
  VkShaderModule ocean_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(oceanprepass_vert);
    create_info.pCode = (const uint32_t *)oceanprepass_vert;
    err = tb_rnd_create_shader(render_system, &create_info,
                               "Ocean Prepass Vert", &oceanprepass_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load ocean prepass vert shader module",
                    err);

    create_info.codeSize = sizeof(oceanprepass_frag);
    create_info.pCode = (const uint32_t *)oceanprepass_frag;
    err = tb_rnd_create_shader(render_system, &create_info,
                               "Ocean Prepass Frag", &oceanprepass_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load ocean prepass frag shader module",
                    err);

    create_info.codeSize = sizeof(ocean_vert);
    create_info.pCode = (const uint32_t *)ocean_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Ocean Vert",
                               &ocean_vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load ocean vert shader module", err);

    create_info.codeSize = sizeof(ocean_frag);
    create_info.pCode = (const uint32_t *)ocean_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Ocean Frag",
                               &ocean_frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load ocean frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = ocean_vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = ocean_frag_mod,
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
                      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
                  },
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[1]){
                      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
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
                      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                      .dstColorBlendFactor =
                          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                      .colorBlendOp = VK_BLEND_OP_ADD,
                      .srcAlphaBlendFactor =
                          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                      .alphaBlendOp = VK_BLEND_OP_ADD,
                      .colorWriteMask =
                          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                  },
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_FALSE,
              .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
              .maxDepthBounds = 1.0f,
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
                                         "Ocean Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean pipeline", err);

  create_info.pStages =
      (VkPipelineShaderStageCreateInfo[2]){
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT,
              .module = oceanprepass_vert_mod,
              .pName = "vert",
          },
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
              .module = oceanprepass_frag_mod,
              .pName = "frag",
          },
      },
  create_info.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
  };
  create_info.pDepthStencilState =
      &(VkPipelineDepthStencilStateCreateInfo){
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_TRUE,
          .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
          .maxDepthBounds = 1.0f,
      },
  create_info.renderPass = prepass;

  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Ocean Prepass Pipeline",
                                         prepass_pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean prepass pipeline", err);

  tb_rnd_destroy_shader(render_system, oceanprepass_vert_mod);
  tb_rnd_destroy_shader(render_system, oceanprepass_frag_mod);

  tb_rnd_destroy_shader(render_system, ocean_vert_mod);
  tb_rnd_destroy_shader(render_system, ocean_frag_mod);

  return err;
}

VkResult create_ocean_shadow_pipeline(RenderSystem *render_system,
                                      VkRenderPass pass,
                                      VkPipelineLayout pipe_layout,
                                      VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(oceanshadow_vert);
    create_info.pCode = (const uint32_t *)oceanshadow_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Ocean Shadow Vert",
                               &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load ocean shadow vert shader module", err);

    create_info.codeSize = sizeof(oceanshadow_frag);
    create_info.pCode = (const uint32_t *)oceanshadow_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Ocean Shadow Frag",
                               &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load ocean shadow frag shader module", err);
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
                  (VkVertexInputBindingDescription[1]){
                      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
                  },
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[1]){
                      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
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
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
              .depthCompareOp = VK_COMPARE_OP_LESS,
              .maxDepthBounds = 1.0f,
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
                                         "Ocean Shadow Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean shadow pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

bool create_ocean_system(OceanSystem *self, const OceanSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which ocean depends on", false);
  MeshSystem *mesh_system =
      tb_get_system(system_deps, system_dep_count, MeshSystem);
  TB_CHECK_RETURN(mesh_system,
                  "Failed to find mesh system which ocean depends on", false);
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  TB_CHECK_RETURN(mesh_system,
                  "Failed to find view system which ocean depends on", false);
  RenderPipelineSystem *render_pipe_system =
      tb_get_system(system_deps, system_dep_count, RenderPipelineSystem);
  TB_CHECK_RETURN(
      render_pipe_system,
      "Failed to find render pipeline system which ocean depends on", false);
  RenderTargetSystem *render_target_system =
      tb_get_system(system_deps, system_dep_count, RenderTargetSystem);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find render target system which ocean depends on",
                  false);

  *self = (OceanSystem){
      .render_system = render_system,
      .render_pipe_system = render_pipe_system,
      .mesh_system = mesh_system,
      .view_system = view_system,
      .render_target_system = render_target_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Load the known glb that has the ocean mesh
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(self->tmp_alloc, "scenes/Ocean.glb");

  // Load glb off disk
  cgltf_data *data = tb_read_glb(self->std_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  // Parse expected mesh from glb
  {
    cgltf_mesh *ocean_mesh = &data->meshes[0];
    ocean_mesh->name = "Ocean";

    self->ocean_transform = tb_transform_from_node(&data->nodes[0]);

    self->ocean_index_type = ocean_mesh->primitives->indices->stride == 2
                                 ? VK_INDEX_TYPE_UINT16
                                 : VK_INDEX_TYPE_UINT32;
    self->ocean_index_count = ocean_mesh->primitives->indices->count;

    uint64_t index_size =
        self->ocean_index_count *
        (self->ocean_index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    uint64_t idx_padding = index_size % (sizeof(uint16_t) * 4);
    self->ocean_pos_offset = index_size + idx_padding;

    self->ocean_patch_mesh =
        tb_mesh_system_load_mesh(mesh_system, asset_path, &data->nodes[0]);
  }

  VkResult err = VK_SUCCESS;

  // Create Immutable Sampler
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
    err = tb_rnd_create_sampler(render_system, &create_info, "Ocean Sampler",
                                &self->sampler);
    TB_VK_CHECK_RET(err, "Failed to create ocean sampler", err);
  }

  // Create ocean descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings = (VkDescriptorSetLayoutBinding[4]){
            {
                .binding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
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
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
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
                                   "Ocean Descriptor Set Layout",
                                   &self->set_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean descriptor set layout", false);
  }

  // Create ocean shadow pipeline layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts =
            (VkDescriptorSetLayout[1]){
                self->set_layout,
            },
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (VkPushConstantRange[1]){
                {
                    .size = sizeof(ShadowViewConstants),
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                },
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Ocean Shadow Pipeline Layout",
                                        &self->shadow_pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean shadow pipeline layout",
                    false);
  }

  // Create ocean pipeline layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts =
            (VkDescriptorSetLayout[2]){
                self->set_layout,
                self->view_system->set_layout,
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Ocean Pipeline Layout",
                                        &self->pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean pipeline layout", false);
  }

  // Retrieve passes
  const TbRenderPassId *shadow_ids = self->render_pipe_system->shadow_passes;
  TbRenderPassId depth_id = self->render_pipe_system->transparent_depth_pass;
  TbRenderPassId color_id = self->render_pipe_system->transparent_color_pass;
  {
    for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
      self->shadow_passes[i] =
          tb_render_pipeline_get_pass(self->render_pipe_system, shadow_ids[i]);
    }

    self->ocean_prepass =
        tb_render_pipeline_get_pass(self->render_pipe_system, depth_id);
    self->ocean_pass =
        tb_render_pipeline_get_pass(self->render_pipe_system, color_id);
  }

  err = create_ocean_shadow_pipeline(render_system, self->shadow_passes[0],
                                     self->shadow_pipe_layout,
                                     &self->shadow_pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean shadow pipeline", false);

  err = create_ocean_pipelines(render_system, self->ocean_prepass,
                               self->ocean_pass, self->pipe_layout,
                               &self->prepass_pipeline, &self->pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean pipeline", false);

  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    self->shadow_draw_ctxs[i] = tb_render_pipeline_register_draw_context(
        render_pipe_system, &(DrawContextDescriptor){
                                .batch_size = sizeof(OceanShadowBatch),
                                .draw_fn = ocean_shadow_record,
                                .pass_id = shadow_ids[i],
                            });
  }
  self->trans_depth_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(OceanDrawBatch),
                              .draw_fn = ocean_prepass_record,
                              .pass_id = depth_id,
                          });
  self->trans_color_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(OceanDrawBatch),
                              .draw_fn = ocean_pass_record,
                              .pass_id = color_id,
                          });

  self->ocean_geom_buffer =
      tb_mesh_system_get_gpu_mesh(mesh_system, self->ocean_patch_mesh);
  TB_CHECK_RETURN(self->ocean_geom_buffer, "Failed to get gpu buffer for mesh",
                  false);

  return true;
}

void destroy_ocean_system(OceanSystem *self) {
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->ocean_patch_mesh);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->render_system,
                                   self->ocean_pools[i].set_pool);
  }

  tb_rnd_destroy_sampler(self->render_system, self->sampler);

  tb_rnd_destroy_pipeline(self->render_system, self->prepass_pipeline);
  tb_rnd_destroy_pipeline(self->render_system, self->pipeline);
  tb_rnd_destroy_pipeline(self->render_system, self->shadow_pipeline);

  tb_rnd_destroy_pipe_layout(self->render_system, self->shadow_pipe_layout);
  tb_rnd_destroy_pipe_layout(self->render_system, self->pipe_layout);
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  *self = (OceanSystem){0};
}

void tick_ocean_system(OceanSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  TracyCZoneNC(ctx, "Ocean System Tick", TracyCategoryColorRendering, true);

  EntityId *ocean_entities = tb_get_column_entity_ids(input, 0);

  const uint32_t ocean_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *oceans =
      tb_get_column_check_id(input, 0, 0, OceanComponentId);

  const uint32_t camera_count = tb_get_column_component_count(input, 1);
  const PackedComponentStore *cameras =
      tb_get_column_check_id(input, 1, 0, CameraComponentId);

  const uint32_t dir_light_count = tb_get_column_component_count(input, 2);
  const PackedComponentStore *dir_light_store =
      tb_get_column_check_id(input, 2, 0, DirectionalLightComponentId);

  if (ocean_count == 0 || camera_count == 0 || dir_light_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Copy the ocean component for output
  OceanComponent *out_oceans =
      tb_alloc_nm_tp(self->tmp_alloc, ocean_count, OceanComponent);
  SDL_memcpy(out_oceans, oceans->components,
             ocean_count * sizeof(OceanComponent));
  // Update time on all ocean components
  for (uint32_t ocean_idx = 0; ocean_idx < ocean_count; ++ocean_idx) {
    OceanComponent *ocean = &out_oceans[ocean_idx];
    ocean->time += delta_seconds;
  }

  VkResult err = VK_SUCCESS;
  RenderSystem *render_system = self->render_system;

  // Allocate and write all ocean descriptor sets
  {
    // Allocate all the descriptor sets
    {
      VkDescriptorPoolCreateInfo pool_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = ocean_count * 8,
          .poolSizeCount = 1,
          .pPoolSizes =
              &(VkDescriptorPoolSize){
                  .descriptorCount = ocean_count * 8,
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              },
      };

      err = tb_rnd_frame_desc_pool_tick(render_system, &pool_info,
                                        self->set_layout, self->ocean_pools,
                                        ocean_count);
      TB_VK_CHECK(err, "Failed to tick ocean's descriptor pool");
    }

    // Just upload and write all views for now, they tend to be important anyway
    const uint32_t write_count = ocean_count * 3;
    VkWriteDescriptorSet *writes =
        tb_alloc_nm_tp(self->tmp_alloc, write_count, VkWriteDescriptorSet);
    VkDescriptorBufferInfo *buffer_info =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, VkDescriptorBufferInfo);
    VkDescriptorImageInfo *depth_info =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, VkDescriptorImageInfo);
    VkDescriptorImageInfo *color_info =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, VkDescriptorImageInfo);
    TbHostBuffer *buffers =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, TbHostBuffer);
    for (uint32_t oc_idx = 0; oc_idx < ocean_count; ++oc_idx) {
      const OceanComponent *ocean_comp =
          tb_get_component(oceans, oc_idx, OceanComponent);
      TbHostBuffer *buffer = &buffers[oc_idx];

      const uint32_t write_idx = oc_idx * 3;

      OceanData data = {
          .time = ocean_comp->time,
          .wave_count = ocean_comp->wave_count,
      };
      transform_to_matrix(&data.m, &self->ocean_transform);
      SDL_memcpy(data.wave, ocean_comp->waves,
                 data.wave_count * sizeof(OceanWave));

      // Write ocean data into the tmp buffer we know will wind up on the GPU
      err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, sizeof(OceanData),
                                             0x40, buffer);
      TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for ocean");

      // Copy view data to the allocated buffer
      SDL_memcpy(buffer->ptr, &data, sizeof(OceanData));

      VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);

      // Get the descriptor we want to write to
      VkDescriptorSet ocean_set = tb_rnd_frame_desc_pool_get_set(
          render_system, self->ocean_pools, oc_idx);

      buffer_info[oc_idx] = (VkDescriptorBufferInfo){
          .buffer = tmp_gpu_buffer,
          .offset = buffer->offset,
          .range = sizeof(OceanData),
      };

      VkImageView depth_view = tb_render_target_get_view(
          self->render_target_system, self->render_system->frame_idx,
          self->render_target_system->depth_buffer_copy);

      VkImageView color_view = tb_render_target_get_view(
          self->render_target_system, self->render_system->frame_idx,
          self->render_target_system->color_copy);

      depth_info[oc_idx] = (VkDescriptorImageInfo){
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView = depth_view,
      };

      color_info[oc_idx] = (VkDescriptorImageInfo){
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView = color_view,
      };

      // Construct write descriptors
      writes[write_idx + 0] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = ocean_set,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo = &buffer_info[oc_idx],
      };
      writes[write_idx + 1] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = ocean_set,
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo = &depth_info[oc_idx],
      };
      writes[write_idx + 2] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = ocean_set,
          .dstBinding = 2,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo = &color_info[oc_idx],
      };
    }
    vkUpdateDescriptorSets(render_system->render_thread->device, write_count,
                           writes, 0, NULL);
  }

  // TODO: Make this less hacky
  const uint32_t width = render_system->render_thread->swapchain.width;
  const uint32_t height = render_system->render_thread->swapchain.height;

  // Draw the ocean
  {
    // Look up the shadow caster's relevant view info
    ShadowViewConstants shadow_consts[TB_CASCADE_COUNT] = {0};
    {
      const DirectionalLightComponent *light =
          tb_get_component(dir_light_store, 0, DirectionalLightComponent);
      for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
        const View *view =
            tb_get_view(self->view_system, light->cascade_views[i]);
        shadow_consts[i].vp = view->view_data.vp;
      }
    }

    // Max camera * ocean draw batches are required
    uint32_t batch_count = 0;
    const uint32_t batch_max = ocean_count * camera_count;
    OceanDrawBatch *batches =
        tb_alloc_nm_tp(self->tmp_alloc, batch_max, OceanDrawBatch);
    OceanDrawBatch *prepass_batches =
        tb_alloc_nm_tp(self->tmp_alloc, batch_max, OceanDrawBatch);
    OceanShadowBatch *shadow_batches = tb_alloc_nm_tp(
        self->tmp_alloc, batch_max * TB_CASCADE_COUNT, OceanShadowBatch);

    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      const CameraComponent *camera =
          tb_get_component(cameras, cam_idx, CameraComponent);

      VkDescriptorSet view_set =
          tb_view_system_get_descriptor(self->view_system, camera->view_id);

      for (uint32_t ocean_idx = 0; ocean_idx < ocean_count; ++ocean_idx) {
        VkDescriptorSet ocean_set = tb_rnd_frame_desc_pool_get_set(
            self->render_system, self->ocean_pools, ocean_idx);

        // TODO: only if ocean is visible to camera
        batches[batch_count] = (OceanDrawBatch){
            .pipeline = self->pipeline,
            .layout = self->pipe_layout,
            .viewport = {0, height, width, -(float)height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .view_set = view_set,
            .ocean_set = ocean_set,
            .geom_buffer = self->ocean_geom_buffer,
            .index_type = (VkIndexType)self->ocean_index_type,
            .index_count = self->ocean_index_count,
            .pos_offset = self->ocean_pos_offset,
        };
        prepass_batches[batch_count] = (OceanDrawBatch){
            .pipeline = self->prepass_pipeline,
            .layout = self->pipe_layout,
            .viewport = {0, height, width, -height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .view_set = view_set,
            .ocean_set = ocean_set,
            .geom_buffer = self->ocean_geom_buffer,
            .index_type = (VkIndexType)self->ocean_index_type,
            .index_count = self->ocean_index_count,
            .pos_offset = self->ocean_pos_offset,
        };
        for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
          shadow_batches[batch_count + i] = (OceanShadowBatch){
              .pipeline = self->shadow_pipeline,
              .layout = self->shadow_pipe_layout,
              .viewport = {0, TB_SHADOW_MAP_DIM, TB_SHADOW_MAP_DIM,
                           -TB_SHADOW_MAP_DIM, 0, 1},
              .scissor = {{0, 0}, {TB_SHADOW_MAP_DIM, TB_SHADOW_MAP_DIM}},
              .ocean_set = ocean_set,
              .shadow_consts = shadow_consts[i],
              .geom_buffer = self->ocean_geom_buffer,
              .index_type = (VkIndexType)self->ocean_index_type,
              .index_count = self->ocean_index_count,
              .pos_offset = self->ocean_pos_offset,
          };
        }
        batch_count++;
      }
    }

    // Draw to the prepass and the ocean pass
    for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
      tb_render_pipeline_issue_draw_batch(self->render_pipe_system,
                                          self->shadow_draw_ctxs[i],
                                          batch_count, &shadow_batches[i]);
    }
    tb_render_pipeline_issue_draw_batch(self->render_pipe_system,
                                        self->trans_depth_draw_ctx, batch_count,
                                        prepass_batches);
    tb_render_pipeline_issue_draw_batch(self->render_pipe_system,
                                        self->trans_color_draw_ctx, batch_count,
                                        batches);

    // Report output (we've updated the time on the ocean component and the
    // world transform state)
    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .id = OceanComponentId,
        .count = ocean_count,
        .components = (uint8_t *)out_oceans,
        .entities = ocean_entities,
    };
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(ocean, OceanSystem, OceanSystemDescriptor)

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc) {
  *desc = (SystemDescriptor){
      .name = "Ocean",
      .size = sizeof(OceanSystem),
      .id = OceanSystemId,
      .desc = (InternalDescriptor)ocean_desc,
      .dep_count = 3,
      .deps[0] = {1, {OceanComponentId}},
      .deps[1] = {2, {CameraComponentId, TransformComponentId}},
      .deps[2] = {1, {DirectionalLightComponentId}},
      .system_dep_count = 5,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = MeshSystemId,
      .system_deps[2] = ViewSystemId,
      .system_deps[3] = RenderPipelineSystemId,
      .system_deps[4] = RenderTargetSystemId,
      .create = tb_create_ocean_system,
      .destroy = tb_destroy_ocean_system,
      .tick = tb_tick_ocean_system,
  };
}
