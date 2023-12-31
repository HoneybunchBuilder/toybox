#include "meshsystem.h"

#include "assetsystem.h"
#include "cameracomponent.h"
#include "cgltf.h"
#include "common.hlsli"
#include "gltf.hlsli"
#include "hash.h"
#include "lightcomponent.h"
#include "materialsystem.h"
#include "meshcomponent.h"
#include "profiling.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbutil.h"
#include "texturesystem.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "vkdbg.h"
#include "world.h"

#include <flecs.h>
#include <meshoptimizer.h>

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "gltf_frag.h"
#include "gltf_vert.h"
#include "opaque_prepass_frag.h"
#include "opaque_prepass_vert.h"
#pragma clang diagnostic pop

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

VkResult create_prepass_pipeline(TbRenderSystem *rnd_sys, VkFormat depth_format,
                                 VkPipelineLayout pipe_layout,
                                 VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(opaque_prepass_vert);
    create_info.pCode = (const uint32_t *)opaque_prepass_vert;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Vert",
                               &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load opaque prepass vert shader module",
                    err);

    create_info.codeSize = sizeof(opaque_prepass_frag);
    create_info.pCode = (const uint32_t *)opaque_prepass_frag;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Frag",
                               &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load opaque prepass frag shader module",
                    err);
  }

  VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;

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
  err = tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                         "Opaque Prepass Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create opaque prepass pipeline", err);

  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return err;
}

VkResult create_mesh_pipelines(TbRenderSystem *rnd_sys, VkFormat color_format,
                               VkFormat depth_format,
                               VkPipelineLayout pipe_layout,
                               VkPipeline *opaque_pipe,
                               VkPipeline *transparent_pipe) {
  VkResult err = VK_SUCCESS;

  // Load Shader Modules
  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_vert),
        .pCode = (const uint32_t *)gltf_vert,
    };
    err = tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Vert", &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to create shader module", err);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_frag),
        .pCode = (const uint32_t *)gltf_frag,
    };
    err = tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag", &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to create shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info_base = {
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
              .cullMode = VK_CULL_MODE_BACK_BIT,
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

  VkGraphicsPipelineCreateInfo opaque_ci = create_info_base;
  VkGraphicsPipelineCreateInfo trans_ci = create_info_base;
  trans_ci.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
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
          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                            VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      }},
  };

  // Create pipelines
  {
    err = tb_rnd_create_graphics_pipelines(rnd_sys, 1, &opaque_ci,
                                           "Opaque Mesh Pipeline", opaque_pipe);
    TB_VK_CHECK_RET(err, "Failed to create opaque pipeline", err);

    err = tb_rnd_create_graphics_pipelines(
        rnd_sys, 1, &trans_ci, "Transparent Mesh Pipeline", transparent_pipe);
    TB_VK_CHECK_RET(err, "Failed to create trans pipeline", err);
  }

  // Can destroy shader moduless
  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return err;
}

void prepass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                    uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Opaque Prepass", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Prepass", 3, true);
  cmd_begin_label(buffer, "Opaque Prepass", (float4){0.0f, 0.0f, 1.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const TbPrimitiveBatch *prim_batch =
        (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Record Mesh", TracyCategoryColorRendering, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 6;
    VkDescriptorSet sets[set_count] = {
        prim_batch->view_set, prim_batch->draw_set, prim_batch->obj_set,
        prim_batch->idx_set,  prim_batch->pos_set,  prim_batch->norm_set};
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            set_count, sets, 0, NULL);

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      TracyCZoneNC(draw_ctx, "Record Indirect Draw",
                   TracyCategoryColorRendering, true);
      vkCmdDrawIndirect(buffer, draw->buffer, draw->offset, draw->draw_count,
                        draw->stride);
      TracyCZoneEnd(draw_ctx);
    }

    cmd_end_label(buffer);
    TracyCZoneEnd(batch_ctx);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
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

    TracyCZoneNC(batch_ctx, "Record Mesh", TracyCategoryColorRendering, true);
    cmd_begin_label(buffer, "Mesh Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 10;
    const VkDescriptorSet sets[set_count] = {
        prim_batch->view_set, prim_batch->mat_set,  prim_batch->draw_set,
        prim_batch->obj_set,  prim_batch->tex_set,  prim_batch->idx_set,
        prim_batch->pos_set,  prim_batch->norm_set, prim_batch->tan_set,
        prim_batch->uv0_set,
    };
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            set_count, sets, 0, NULL);
    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      TracyCZoneNC(draw_ctx, "Record Indirect Draw",
                   TracyCategoryColorRendering, true);
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      vkCmdDrawIndirect(buffer, draw->buffer, draw->offset, draw->draw_count,
                        draw->stride);
      TracyCZoneEnd(draw_ctx);
    }

    cmd_end_label(buffer);
    TracyCZoneEnd(batch_ctx);
  }
}

void opaque_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Opaque Mesh Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Meshes", 3, true);
  cmd_begin_label(buffer, "Opaque Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void transparent_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                             uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Transparent Mesh Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Transparent Meshes", 3,
                    true);
  cmd_begin_label(buffer, "Transparent Meshes",
                  (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

TbMeshSystem create_mesh_system_internal(
    TbAllocator std_alloc, TbAllocator tmp_alloc, TbRenderSystem *rnd_sys,
    TbMaterialSystem *mat_sys, TbTextureSystem *tex_sys, TbViewSystem *view_sys,
    TbRenderObjectSystem *ro_sys, TbRenderPipelineSystem *rp_sys) {
  TbMeshSystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .rnd_sys = rnd_sys,
      .material_system = mat_sys,
      .view_sys = view_sys,
      .render_object_system = ro_sys,
      .rp_sys = rp_sys,
  };
  TB_DYN_ARR_RESET(sys.meshes, std_alloc, 8);
  TbRenderPassId prepass_id = rp_sys->opaque_depth_normal_pass;
  TbRenderPassId opaque_pass_id = rp_sys->opaque_color_pass;
  TbRenderPassId transparent_pass_id = rp_sys->transparent_color_pass;

  // Setup mesh system for rendering
  {
    VkResult err = VK_SUCCESS;

    // Create mesh descriptor set layout
    {
      const VkDescriptorBindingFlags flags =
          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
          VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
      VkDescriptorSetLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .pNext =
              &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                  .bindingCount = 1,
                  .pBindingFlags = (VkDescriptorBindingFlags[1]){flags},
              },
          .bindingCount = 1,
          .pBindings =
              (VkDescriptorSetLayoutBinding[1]){
                  {
                      .binding = 0,
                      .descriptorCount = 4096,
                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                  },
              },
      };
      err = tb_rnd_create_set_layout(rnd_sys, &create_info, "Mesh Attr Layout",
                                     &sys.mesh_set_layout);
      TB_VK_CHECK(err, "Failed to create mesh attr set layout");
    }

    // Create instance descriptor set layout
    {
      VkDescriptorSetLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 1,
          .pBindings =
              (VkDescriptorSetLayoutBinding[1]){
                  {
                      .binding = 0,
                      .descriptorCount = 1,
                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
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
                  view_sys->set_layout,
                  sys.draw_set_layout,
                  ro_sys->set_layout,
                  sys.mesh_set_layout,
                  sys.mesh_set_layout,
                  sys.mesh_set_layout,
              },
      };
      err = tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                          "Opaque Depth Normal Prepass Layout",
                                          &sys.prepass_layout);
      TB_VK_CHECK(err, "Failed to create opaque prepass set layout");
    }

    // Create prepass pipeline
    {
      VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
      err = create_prepass_pipeline(sys.rnd_sys, depth_format,
                                    sys.prepass_layout, &sys.prepass_pipe);
      TB_VK_CHECK(err, "Failed to create opaque prepass pipeline");
    }

    // Create pipeline layouts
    {
      const uint32_t layout_count = 10;
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = layout_count,
          .pSetLayouts =
              (VkDescriptorSetLayout[layout_count]){
                  view_sys->set_layout,
                  mat_sys->set_layout,
                  sys.draw_set_layout,
                  ro_sys->set_layout,
                  tex_sys->set_layout,
                  sys.mesh_set_layout,
                  sys.mesh_set_layout,
                  sys.mesh_set_layout,
                  sys.mesh_set_layout,
                  sys.mesh_set_layout,
              },
      };
      tb_rnd_create_pipeline_layout(rnd_sys, &create_info,
                                    "GLTF Pipeline Layout", &sys.pipe_layout);
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

      VkFormat depth_format = tb_render_target_get_format(
          sys.rp_sys->rt_sys, depth_info.attachment);

      VkFormat color_format = VK_FORMAT_UNDEFINED;
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
          color_format = format;
          break;
        }
      }
      err = create_mesh_pipelines(sys.rnd_sys, color_format, depth_format,
                                  sys.pipe_layout, &sys.opaque_pipeline,
                                  &sys.transparent_pipeline);
      TB_VK_CHECK(err, "Failed to create mesh pipelines");
    }
  }
  // Register drawing with the pipelines
  sys.prepass_draw_ctx2 = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(TbPrimitiveBatch),
                  .draw_fn = prepass_record,
                  .pass_id = prepass_id,
              });
  sys.opaque_draw_ctx2 = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(TbPrimitiveBatch),
                  .draw_fn = opaque_pass_record,
                  .pass_id = opaque_pass_id,
              });
  sys.transparent_draw_ctx2 = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(TbPrimitiveBatch),
                  .draw_fn = transparent_pass_record,
                  .pass_id = transparent_pass_id,
              });
  return sys;
}

void destroy_mesh_system(TbMeshSystem *self) {
  TbRenderSystem *rnd_sys = self->rnd_sys;

  tb_rnd_destroy_pipeline(rnd_sys, self->opaque_pipeline);
  tb_rnd_destroy_pipeline(rnd_sys, self->transparent_pipeline);
  tb_rnd_destroy_pipeline(rnd_sys, self->prepass_pipe);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->pipe_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->prepass_layout);

  TB_DYN_ARR_FOREACH(self->meshes, i) {
    if (TB_DYN_ARR_AT(self->meshes, i).ref_count != 0) {
      TB_CHECK(false, "Leaking meshes");
    }
  }

  TB_DYN_ARR_DESTROY(self->meshes);

  *self = (TbMeshSystem){0};
}

uint32_t find_mesh_by_id(TbMeshSystem *self, uint64_t id) {
  TB_DYN_ARR_FOREACH(self->meshes, i) {
    if (TB_DYN_ARR_AT(self->meshes, i).id.id == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

// Based on an example from this cgltf commit message:
// https://github.com/jkuhlmann/cgltf/commit/bd8bd2c9cc08ff9b75a9aa9f99091f7144665c60
static cgltf_result decompress_buffer_view(TbAllocator alloc,
                                           cgltf_buffer_view *view) {
  if (view->data != NULL) {
    // Already decoded
    return cgltf_result_success;
  }

  // Uncompressed buffer? No problem
  if (!view->has_meshopt_compression) {
    uint8_t *data = (uint8_t *)view->buffer->data;
    data += view->offset;

    uint8_t *result = tb_alloc(alloc, view->size);
    SDL_memcpy(result, data, view->size); // NOLINT
    view->data = result;
    return cgltf_result_success;
  }

  const cgltf_meshopt_compression *mc = &view->meshopt_compression;
  uint8_t *data = (uint8_t *)mc->buffer->data;
  data += mc->offset;
  TB_CHECK_RETURN(data, "Invalid data", cgltf_result_invalid_gltf);

  uint8_t *result = tb_alloc(alloc, mc->count * mc->stride);
  TB_CHECK_RETURN(result, "Failed to allocate space for decoded buffer view",
                  cgltf_result_out_of_memory);

  int32_t res = -1;
  switch (mc->mode) {
  default:
  case cgltf_meshopt_compression_mode_invalid:
    break;

  case cgltf_meshopt_compression_mode_attributes:
    res = meshopt_decodeVertexBuffer(result, mc->count, mc->stride, data,
                                     mc->size);
    break;

  case cgltf_meshopt_compression_mode_triangles:
    res = meshopt_decodeIndexBuffer(result, mc->count, mc->stride, data,
                                    mc->size);
    break;

  case cgltf_meshopt_compression_mode_indices:
    res = meshopt_decodeIndexSequence(result, mc->count, mc->stride, data,
                                      mc->size);
    break;
  }
  TB_CHECK_RETURN(res == 0, "Failed to decode buffer view",
                  cgltf_result_io_error);

  switch (mc->filter) {
  default:
  case cgltf_meshopt_compression_filter_none:
    break;

  case cgltf_meshopt_compression_filter_octahedral:
    meshopt_decodeFilterOct(result, mc->count, mc->stride);
    break;

  case cgltf_meshopt_compression_filter_quaternion:
    meshopt_decodeFilterQuat(result, mc->count, mc->stride);
    break;

  case cgltf_meshopt_compression_filter_exponential:
    meshopt_decodeFilterExp(result, mc->count, mc->stride);
    break;
  }

  view->data = result;

  return cgltf_result_success;
}

TbMeshId tb_mesh_system_load_mesh(TbMeshSystem *self, const char *path,
                                  const cgltf_node *node) {
  // Hash the mesh's path and the cgltf_mesh structure to get
  // an id We'd prefer to use a name but gltfpack is currently
  // stripping mesh names
  const cgltf_mesh *mesh = node->mesh;
  TB_CHECK_RETURN(mesh, "Given node has no mesh", TbInvalidMeshId);

  uint64_t id = tb_hash(0, (const uint8_t *)path, SDL_strlen(path));
  id = tb_hash(id, (const uint8_t *)mesh, sizeof(cgltf_mesh));

  uint32_t mesh_idx = find_mesh_by_id(self, id);
  if (mesh_idx != SDL_MAX_UINT32) {
    // Mesh was found, just return that
    TB_DYN_ARR_AT(self->meshes, mesh_idx).ref_count++;
    return (TbMeshId){id, mesh_idx};
  }

  // Mesh was not found, load it now
  mesh_idx = TB_DYN_ARR_SIZE(self->meshes);
  {
    TbMesh m = {.id = {.id = id, .idx = mesh_idx}};
    TB_DYN_ARR_APPEND(self->meshes, m);
  }
  TbMesh *tb_mesh = &TB_DYN_ARR_AT(self->meshes, mesh_idx);

  // Determine how big this mesh is
  uint64_t index_size = 0;
  uint64_t geom_size = 0;
  uint64_t attr_size_per_type[cgltf_attribute_type_max_enum] = {0};
  {
    // Determine mesh index type
    {
      tb_auto stride = mesh->primitives[0].indices->stride;
      if (stride == sizeof(uint16_t)) {
        tb_mesh->idx_type = VK_INDEX_TYPE_UINT16;
      } else if (stride == sizeof(uint32_t)) {
        tb_mesh->idx_type = VK_INDEX_TYPE_UINT32;
      } else {
        TB_CHECK(false, "Unexpected index stride");
      }
    }

    uint64_t vertex_size = 0;
    uint32_t vertex_count = 0;
    for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
         ++prim_idx) {
      cgltf_primitive *prim = &mesh->primitives[prim_idx];
      cgltf_accessor *indices = prim->indices;
      cgltf_size idx_size =
          tb_calc_aligned_size(indices->count, indices->stride, 16);

      index_size += idx_size;
      vertex_count = prim->attributes[0].data->count;

      for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
           ++attr_idx) {
        // Only care about certain attributes at the moment
        cgltf_attribute_type type = prim->attributes[attr_idx].type;
        int32_t idx = prim->attributes[attr_idx].index;
        if ((type == cgltf_attribute_type_position ||
             type == cgltf_attribute_type_normal ||
             type == cgltf_attribute_type_tangent ||
             type == cgltf_attribute_type_texcoord) &&
            idx == 0) {
          cgltf_accessor *attr = prim->attributes[attr_idx].data;
          uint64_t attr_size = vertex_count * attr->stride;
          attr_size_per_type[type] += attr_size;
        }
      }

      for (uint32_t i = 0; i < cgltf_attribute_type_max_enum; ++i) {
        tb_auto attr_size = attr_size_per_type[i];
        if (attr_size > 0) {
          attr_size_per_type[i] = tb_calc_aligned_size(1, attr_size, 16);
          vertex_size += attr_size_per_type[i];
        }
      }
    }

    geom_size = index_size + vertex_size;
  }

  uint64_t attr_offset_per_type[cgltf_attribute_type_max_enum] = {0};
  {
    uint64_t offset = index_size;
    for (uint32_t i = 0; i < cgltf_attribute_type_max_enum; ++i) {
      tb_auto attr_size = attr_size_per_type[i];
      if (attr_size > 0) {
        attr_offset_per_type[i] = offset;
        offset += attr_size;
      }
    }
  }

  VkResult err = VK_SUCCESS;

  // Create space for the mesh on the GPU
  void *ptr = NULL;
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = geom_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    err = tb_rnd_sys_create_gpu_buffer(self->rnd_sys, &create_info, mesh->name,
                                       &tb_mesh->gpu_buffer,
                                       &tb_mesh->host_buffer, &ptr);
    TB_VK_CHECK_RET(err, "Failed to create mesh buffer", TbInvalidMeshId);
  }

  // Read the cgltf mesh into the driver owned memory
  {
    uint64_t idx_offset = 0;
    uint64_t vertex_count = 0;
    for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
         ++prim_idx) {
      cgltf_primitive *prim = &mesh->primitives[prim_idx];
      {
        cgltf_accessor *indices = prim->indices;
        cgltf_buffer_view *view = indices->buffer_view;
        cgltf_size src_size = indices->count * indices->stride;
        cgltf_size padded_size =
            tb_calc_aligned_size(indices->count, indices->stride, 16);

        // Decode the buffer
        cgltf_result res = decompress_buffer_view(self->std_alloc, view);
        TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

        void *src = ((uint8_t *)view->data) + indices->offset;
        void *dst = ((uint8_t *)(ptr)) + idx_offset;
        SDL_memcpy(dst, src, src_size); // NOLINT
        idx_offset += padded_size;
      }

      // Determine the order of attributes
      cgltf_size attr_order[4] = {0};
      {
        const cgltf_attribute_type req_order[4] = {
            cgltf_attribute_type_position,
            cgltf_attribute_type_normal,
            cgltf_attribute_type_tangent,
            cgltf_attribute_type_texcoord,
        };
        cgltf_size attr_target_idx = 0;
        for (uint32_t i = 0; i < 4; ++i) {
          bool found = false;
          for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
               ++attr_idx) {
            cgltf_attribute *attr = &prim->attributes[attr_idx];
            if (attr->type == req_order[i]) {
              attr_order[attr_target_idx] = attr_idx;
              attr_target_idx++;
              found = true;
            }
            if (found) {
              break;
            }
          }
        }
      }

      for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
           ++attr_idx) {
        cgltf_attribute *attr = &prim->attributes[attr_order[attr_idx]];
        cgltf_accessor *accessor = attr->data;
        cgltf_buffer_view *view = accessor->buffer_view;

        uint64_t mesh_vert_offset = vertex_count * attr->data->stride;
        uint64_t vtx_offset =
            attr_offset_per_type[attr->type] + mesh_vert_offset;

        size_t src_size = accessor->stride * accessor->count;

        // Decode the buffer
        cgltf_result res = decompress_buffer_view(self->std_alloc, view);
        TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

        void *src = ((uint8_t *)view->data) + accessor->offset;
        void *dst = ((uint8_t *)(ptr)) + vtx_offset;
        SDL_memcpy(dst, src, src_size); // NOLINT
      }

      vertex_count += prim->attributes[0].data->count;
    }

    // Construct one write per primitive
    {
      static const VkFormat
          attr_formats_per_type[cgltf_attribute_type_max_enum] = {
              VK_FORMAT_UNDEFINED,      VK_FORMAT_R16G16B16A16_SINT,
              VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_SNORM,
              VK_FORMAT_R16G16_SINT,
          };

      // Create one buffer view for indices
      {
        VkFormat idx_format = VK_FORMAT_R16_UINT;
        if (tb_mesh->idx_type == VK_INDEX_TYPE_UINT32) {
          idx_format = VK_FORMAT_R32_UINT;
        }
        VkBufferViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .buffer = tb_mesh->gpu_buffer.buffer,
            .offset = 0,
            .range = index_size,
            .format = idx_format,
        };
        tb_rnd_create_buffer_view(self->rnd_sys, &create_info,
                                  "Mesh Index View", &tb_mesh->index_view);
      }

      for (size_t attr_idx = 0; attr_idx < mesh->primitives[0].attributes_count;
           ++attr_idx) {
        cgltf_attribute *attr = &mesh->primitives[0].attributes[attr_idx];

        // Create a buffer view per attribute
        VkBufferViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .buffer = tb_mesh->gpu_buffer.buffer,
            .offset = attr_offset_per_type[attr->type],
            .range = VK_WHOLE_SIZE,
            .format = attr_formats_per_type[attr->type],
        };
        tb_rnd_create_buffer_view(self->rnd_sys, &create_info,
                                  "Mesh Attribute View",
                                  &tb_mesh->attr_views[attr->type - 1]);
      }
    }

    // Make sure to flush the gpu alloc if necessary
    tb_flush_alloc(self->rnd_sys, tb_mesh->gpu_buffer.alloc);
  }

  TB_DYN_ARR_AT(self->meshes, mesh_idx).ref_count++;
  return (TbMeshId){id, mesh_idx};
}

bool tb_mesh_system_take_mesh_ref(TbMeshSystem *self, TbMeshId id) {
  uint32_t index = id.idx;
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh", false);
  TB_DYN_ARR_AT(self->meshes, index).ref_count++;
  return true;
}

VkBuffer tb_mesh_system_get_gpu_mesh(TbMeshSystem *self, TbMeshId id) {
  uint32_t index = id.idx;
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh",
                  VK_NULL_HANDLE);

  VkBuffer buffer = TB_DYN_ARR_AT(self->meshes, index).gpu_buffer.buffer;
  TB_CHECK_RETURN(buffer, "Failed to retrieve buffer", VK_NULL_HANDLE);

  return buffer;
}

void tb_mesh_system_release_mesh_ref(TbMeshSystem *self, TbMeshId id) {
  uint32_t index = id.idx;

  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find mesh");
    return;
  }

  TbMesh *mesh = &TB_DYN_ARR_AT(self->meshes, index);

  if (mesh->ref_count == 0) {
    TB_CHECK(false, "Tried to release reference to mesh with "
                    "0 ref count");
    return;
  }

  mesh->ref_count--;

  if (mesh->ref_count == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->rnd_sys->vma_alloc;

    TbHostBuffer *host_buf = &mesh->host_buffer;
    TbBuffer *gpu_buf = &mesh->gpu_buffer;

    vmaUnmapMemory(vma_alloc, host_buf->alloc);

    vmaDestroyBuffer(vma_alloc, host_buf->buffer, host_buf->alloc);
    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);

    *host_buf = (TbHostBuffer){0};
    *gpu_buf = (TbBuffer){0};
  }
}

VkDescriptorSet tb_mesh_system_get_idx_set(TbMeshSystem *self) {
  return self->mesh_pool.sets[0];
}
VkDescriptorSet tb_mesh_system_get_pos_set(TbMeshSystem *self) {
  return self->mesh_pool.sets[1];
}
VkDescriptorSet tb_mesh_system_get_norm_set(TbMeshSystem *self) {
  return self->mesh_pool.sets[2];
}
VkDescriptorSet tb_mesh_system_get_tan_set(TbMeshSystem *self) {
  return self->mesh_pool.sets[3];
}
VkDescriptorSet tb_mesh_system_get_uv0_set(TbMeshSystem *self) {
  return self->mesh_pool.sets[4];
}

void mesh_descriptor_update(ecs_iter_t *it) {
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, TbMeshSystem);
  ECS_COMPONENT(ecs, TbRenderSystem);

  tb_auto mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  // If the number of meshes has grown to the point where we have run out of
  // space in the descriptor pool we must reallocate. That means destroying the
  // old pool and creating a new pool for all the new writes
  const uint32_t view_count = TB_INPUT_PERM_COUNT + 1; // +1 for index buffer
  const uint64_t incoming_mesh_count = TB_DYN_ARR_SIZE(mesh_sys->meshes);
  const uint64_t incoming_cap = incoming_mesh_count * view_count;
  if (incoming_cap > mesh_sys->mesh_pool.capacity) {
    mesh_sys->mesh_pool.capacity = incoming_cap;
    const uint64_t desc_count = mesh_sys->mesh_pool.capacity;

    // Re-create pool and allocate the one set that everything will be bound to
    {
      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = view_count,
          .poolSizeCount = 1,
          .pPoolSizes =
              (VkDescriptorPoolSize[1]){
                  {
                      .type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                      .descriptorCount = desc_count * view_count,
                  },
              },
      };

      VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
          .sType =
              VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
          .descriptorSetCount = view_count,
          .pDescriptorCounts =
              (uint32_t[view_count]){incoming_mesh_count, incoming_mesh_count,
                                     incoming_mesh_count, incoming_mesh_count,
                                     incoming_mesh_count},
      };
      VkDescriptorSetLayout layouts[view_count] = {
          mesh_sys->mesh_set_layout, mesh_sys->mesh_set_layout,
          mesh_sys->mesh_set_layout, mesh_sys->mesh_set_layout,
          mesh_sys->mesh_set_layout};
      tb_rnd_resize_desc_pool(rnd_sys, &create_info, layouts, &alloc_info,
                              &mesh_sys->mesh_pool, view_count);
    }
  }
  if (incoming_mesh_count <= mesh_sys->mesh_desc_count) {
    // No work to do :)
    return;
  }
  mesh_sys->mesh_desc_count = incoming_mesh_count;

  // Write the index view descriptor
  {
    tb_auto mesh_count = TB_DYN_ARR_SIZE(mesh_sys->meshes);
    tb_auto index_views =
        tb_alloc_nm_tp(mesh_sys->tmp_alloc, mesh_count, VkBufferView);

    TB_DYN_ARR_FOREACH(mesh_sys->meshes, i) {
      tb_auto mesh = &TB_DYN_ARR_AT(mesh_sys->meshes, i);
      index_views[i] = mesh->index_view;
    }

    tb_auto write = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .descriptorCount = mesh_count,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        .dstSet = mesh_sys->mesh_pool.sets[0],
        .dstBinding = 0,
        .pTexelBufferView = index_views,
    };

    tb_rnd_update_descriptors(rnd_sys, 1, &write);
  }

  // Just write all mesh vertex info to the descriptor set
  VkWriteDescriptorSet writes[TB_INPUT_PERM_COUNT] = {0};
  for (uint32_t attr_idx = 0; attr_idx < TB_INPUT_PERM_COUNT; ++attr_idx) {
    tb_auto mesh_count = TB_DYN_ARR_SIZE(mesh_sys->meshes);
    tb_auto buffer_views =
        tb_alloc_nm_tp(mesh_sys->tmp_alloc, mesh_count, VkBufferView);

    TB_DYN_ARR_FOREACH(mesh_sys->meshes, i) {
      tb_auto mesh = &TB_DYN_ARR_AT(mesh_sys->meshes, i);
      buffer_views[i] = mesh->attr_views[attr_idx];
    }

    writes[attr_idx] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .descriptorCount = mesh_count,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        .dstSet = mesh_sys->mesh_pool.sets[attr_idx + 1],
        .dstBinding = 0,
        .pTexelBufferView = buffer_views,
    };
  }
  tb_rnd_update_descriptors(rnd_sys, TB_INPUT_PERM_COUNT, writes);
}

void mesh_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Mesh Draw Tick", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, TbDirectionalLightComponent);
  ECS_COMPONENT(ecs, TbMeshSystem);
  ECS_COMPONENT(ecs, TbMaterialSystem);
  ECS_COMPONENT(ecs, TbTextureSystem);
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbViewSystem);

  tb_auto *mesh_sys = ecs_field(it, TbMeshSystem, 1);
  tb_auto *ro_sys = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  tb_auto *tex_sys = ecs_singleton_get_mut(ecs, TbTextureSystem);
  tb_auto *mat_sys = ecs_singleton_get_mut(ecs, TbMaterialSystem);
  tb_auto *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto *rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  // For each camera
  tb_auto camera_it = ecs_query_iter(ecs, mesh_sys->camera_query);
  while (ecs_query_next(&camera_it)) {
    tb_auto *cameras = ecs_field(&camera_it, TbCameraComponent, 1);
    for (int32_t cam_idx = 0; cam_idx < camera_it.count; ++cam_idx) {
      tb_auto *camera = &cameras[cam_idx];
      tb_auto view_set =
          tb_view_system_get_descriptor(view_sys, camera->view_id);

      const float width = camera->width;
      const float height = camera->height;

      // Run query to determine how many meshes so we can pre-allocate space for
      // batches
      tb_auto mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
      uint32_t opaque_draw_count = 0;
      uint32_t trans_draw_count = 0;
      while (ecs_query_next(&mesh_it)) {
        tb_auto *meshes = ecs_field(&mesh_it, TbMeshComponent, 1);
        for (tb_auto mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
          tb_auto *mesh = &meshes[mesh_idx];

          for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count;
               ++submesh_idx) {
            tb_auto *sm = &mesh->submeshes[submesh_idx];
            tb_auto perm = tb_mat_system_get_perm(mat_sys, sm->material);

            if (perm & GLTF_PERM_ALPHA_CLIP || perm & GLTF_PERM_ALPHA_BLEND) {
              trans_draw_count += mesh->submesh_count;
            } else {
              opaque_draw_count += mesh->submesh_count;
            }
          }
        }
      }
      mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);

      const uint32_t max_draw_count = opaque_draw_count + trans_draw_count;
      if (max_draw_count == 0) {
        continue;
      }

      tb_auto obj_set = tb_render_object_sys_get_set(ro_sys);
      tb_auto tex_set = tb_tex_sys_get_set(tex_sys);
      tb_auto mat_set = tb_mat_system_get_set(mat_sys);
      tb_auto idx_set = tb_mesh_system_get_idx_set(mesh_sys);
      tb_auto pos_set = tb_mesh_system_get_pos_set(mesh_sys);
      tb_auto norm_set = tb_mesh_system_get_norm_set(mesh_sys);
      tb_auto tan_set = tb_mesh_system_get_tan_set(mesh_sys);
      tb_auto uv0_set = tb_mesh_system_get_uv0_set(mesh_sys);

      // Allocate indirect draw buffers
      VkDrawIndirectCommand *opaque_draw_cmds = NULL;
      uint64_t opaque_cmds_offset = 0;
      uint32_t opaque_cmd_count = 0;
      {
        uint64_t size = sizeof(VkDrawIndirectCommand) * opaque_draw_count;
        tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, size, 0x40, &opaque_cmds_offset,
                                       (void **)&opaque_draw_cmds);
      }
      VkDrawIndirectCommand *trans_draw_cmds = NULL;
      uint64_t trans_cmds_offset = 0;
      uint32_t trans_cmd_count = 0;
      {
        uint64_t size = sizeof(VkDrawIndirectCommand) * trans_draw_count;
        tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, size, 0x40, &trans_cmds_offset,
                                       (void **)&trans_draw_cmds);
      }

      // Allocate per-draw storage buffers
      TbGLTFDrawData *opaque_draw_data = NULL;
      const uint64_t opaque_data_size =
          sizeof(TbGLTFDrawData) * opaque_draw_count;
      uint64_t opaque_data_offset = 0;
      tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, opaque_data_size, 0x40,
                                     &opaque_data_offset,
                                     (void **)&opaque_draw_data);

      TbGLTFDrawData *trans_draw_data = NULL;
      const uint64_t trans_data_size =
          sizeof(TbGLTFDrawData) * trans_draw_count;
      uint64_t trans_data_offset = 0;
      tb_rnd_sys_copy_to_tmp_buffer2(rnd_sys, trans_data_size, 0x40,
                                     &trans_data_offset,
                                     (void **)&trans_draw_data);

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
        tb_rnd_frame_desc_pool_tick(rnd_sys, &create_info, layouts, NULL,
                                    mesh_sys->draw_pools.pools, set_count);
      }

      TracyCZoneN(ctx2, "Iterate Meshes", true);
      while (ecs_query_next(&mesh_it)) {
        tb_auto *meshes = ecs_field(&mesh_it, TbMeshComponent, 1);
        for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
          tb_auto *mesh = &meshes[mesh_idx];
          tb_auto entity = mesh_it.entities[mesh_idx];
          tb_auto *ro = ecs_get_mut(ecs, entity, TbRenderObject);

          for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count;
               ++submesh_idx) {
            tb_auto *sm = &mesh->submeshes[submesh_idx];
            tb_auto perm = tb_mat_system_get_perm(mat_sys, sm->material);

            // Deduce whether to write to opaque or transparent data
            tb_auto draw_cmds = opaque_draw_cmds;
            tb_auto draw_count = &opaque_cmd_count;
            tb_auto draw_data = opaque_draw_data;
            if (perm & GLTF_PERM_ALPHA_CLIP || perm & GLTF_PERM_ALPHA_BLEND) {
              draw_cmds = trans_draw_cmds;
              draw_count = &trans_cmd_count;
              draw_data = trans_draw_data;
            }

            // Write a command and a piece of draw data into the buffers
            tb_auto draw_idx = *draw_count;
            draw_cmds[draw_idx] = (VkDrawIndirectCommand){
                .vertexCount = sm->index_count,
                .instanceCount = 1,
            };
            draw_data[draw_idx] = (TbGLTFDrawData){
                .perm = sm->vertex_perm,
                .obj_idx = ro->index,
                .mesh_idx = mesh->mesh_id.idx,
                .mat_idx = sm->material.idx,
                .index_offset = sm->index_offset,
                .vertex_offset = sm->vertex_offset,
            };
            (*draw_count) += 1;
          }
        }
      }
      TracyCZoneEnd(ctx2);

      VkDescriptorSet opaque_draw_set = tb_rnd_frame_desc_pool_get_set(
          rnd_sys, mesh_sys->draw_pools.pools, 0);
      VkDescriptorSet trans_draw_set = tb_rnd_frame_desc_pool_get_set(
          rnd_sys, mesh_sys->draw_pools.pools, 1);

      // Opaque batch is a bit special since we need to share with the shadow
      // system
      mesh_sys->opaque_batch = tb_alloc_tp(mesh_sys->tmp_alloc, TbDrawBatch);
      tb_auto opaque_prim_batch =
          tb_alloc_tp(mesh_sys->tmp_alloc, TbPrimitiveBatch);

      *opaque_prim_batch = (TbPrimitiveBatch){
          .view_set = view_set,
          .mat_set = mat_set,
          .draw_set = opaque_draw_set,
          .obj_set = obj_set,
          .tex_set = tex_set,
          .idx_set = idx_set,
          .pos_set = pos_set,
          .norm_set = norm_set,
          .tan_set = tan_set,
          .uv0_set = uv0_set,
      };

      // Define batches
      tb_auto opaque_draw = tb_alloc_tp(mesh_sys->tmp_alloc, TbIndirectDraw);
      *opaque_draw =
          (TbIndirectDraw){
              .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
              .draw_count = opaque_cmd_count,
              .offset = opaque_cmds_offset,
              .stride = sizeof(VkDrawIndirectCommand),
          },

      *mesh_sys->opaque_batch = (TbDrawBatch){
          .layout = mesh_sys->pipe_layout,
          .pipeline = mesh_sys->opaque_pipeline,
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch = opaque_prim_batch,
          .draw_count = 1,
          .draw_size = sizeof(TbIndirectDraw),
          .draws = opaque_draw,
          .draw_max = 1,
      };

      TbDrawBatch trans_batch = {
          .layout = mesh_sys->pipe_layout,
          .pipeline = mesh_sys->transparent_pipeline,
          .viewport = {0, height, width, -(float)height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .user_batch =
              &(TbPrimitiveBatch){
                  .view_set = view_set,
                  .mat_set = mat_set,
                  .draw_set = trans_draw_set,
                  .obj_set = obj_set,
                  .tex_set = tex_set,
                  .idx_set = idx_set,
                  .pos_set = pos_set,
                  .norm_set = norm_set,
                  .tan_set = tan_set,
                  .uv0_set = uv0_set,
              },
          .draw_count = 1,
          .draw_size = sizeof(TbIndirectDraw),
          .draws =
              &(TbIndirectDraw){
                  .buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys),
                  .draw_count = trans_cmd_count,
                  .offset = trans_cmds_offset,
                  .stride = sizeof(VkDrawIndirectCommand),
              },
          .draw_max = 1,
      };

      // Prepass batch is the same as opaque but with different pipeline
      tb_auto prepass_batch = *mesh_sys->opaque_batch;
      {
        prepass_batch.pipeline = mesh_sys->prepass_pipe;
        prepass_batch.layout = mesh_sys->prepass_layout;
      }

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

      {
        TracyCZoneN(ctx2, "Submit Batches", true);
        if (opaque_data_size > 0) {
          TbDrawContextId prepass_ctx2 = mesh_sys->prepass_draw_ctx2;
          tb_render_pipeline_issue_draw_batch(rp_sys, prepass_ctx2, 1,
                                              &prepass_batch);

          TbDrawContextId opaque_ctx2 = mesh_sys->opaque_draw_ctx2;
          tb_render_pipeline_issue_draw_batch(rp_sys, opaque_ctx2, 1,
                                              mesh_sys->opaque_batch);
        }
        if (trans_data_size > 0) {
          TbDrawContextId trans_ctx2 = mesh_sys->transparent_draw_ctx2;
          tb_render_pipeline_issue_draw_batch(rp_sys, trans_ctx2, 1,
                                              &trans_batch);
        }
        TracyCZoneEnd(ctx2);
      }
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_register_mesh_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbMaterialSystem);
  ECS_COMPONENT(ecs, TbTextureSystem);
  ECS_COMPONENT(ecs, TbViewSystem);
  ECS_COMPONENT(ecs, TbRenderObjectSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbDirectionalLightComponent);
  ECS_COMPONENT(ecs, TbMeshSystem);

  tb_auto *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto *mat_sys = ecs_singleton_get_mut(ecs, TbMaterialSystem);
  tb_auto *tex_sys = ecs_singleton_get_mut(ecs, TbTextureSystem);
  tb_auto *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto *ro_sys = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  tb_auto *rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);

  tb_auto sys =
      create_mesh_system_internal(world->std_alloc, world->tmp_alloc, rnd_sys,
                                  mat_sys, tex_sys, view_sys, ro_sys, rp_sys);
  sys.camera_query = ecs_query(ecs, {.filter.terms = {
                                         {.id = ecs_id(TbCameraComponent)},
                                     }});
  sys.mesh_query = ecs_query(ecs, {
                                      .filter.terms =
                                          {
                                              {
                                                  .id = ecs_id(TbMeshComponent),
                                                  .inout = EcsInOut,
                                              },
                                          },
                                  });
  sys.dir_light_query =
      ecs_query(ecs, {.filter.terms = {
                          {.id = ecs_id(TbDirectionalLightComponent)},
                      }});

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbMeshSystem), TbMeshSystem, &sys);

  ECS_SYSTEM(ecs, mesh_descriptor_update, EcsOnUpdate,
             TbMeshSystem(TbMeshSystem));
  ECS_SYSTEM(ecs, mesh_draw_tick, EcsOnUpdate, TbMeshSystem(TbMeshSystem));
}

void tb_unregister_mesh_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbMeshSystem);
  TbMeshSystem *sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  ecs_query_fini(sys->dir_light_query);
  ecs_query_fini(sys->mesh_query);
  ecs_query_fini(sys->camera_query);
  destroy_mesh_system(sys);
  ecs_singleton_remove(ecs, TbMeshSystem);
}
