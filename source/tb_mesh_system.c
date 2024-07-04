#include "tb_mesh_system.h"

#include "cgltf.h"
#include "common.hlsli"
#include "gltf.hlsli"
#include "tb_camera_component.h"
#include "tb_gltf.h"
#include "tb_hash.h"
#include "tb_light_component.h"
#include "tb_material_system.h"
#include "tb_mesh_component.h"
#include "tb_mesh_system2.h"
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
#include "gltf_frag.h"
#include "gltf_vert.h"
#include "opaque_prepass_frag.h"
#include "opaque_prepass_vert.h"
#pragma clang diagnostic pop

ECS_COMPONENT_DECLARE(TbMeshSystem);

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
TB_REGISTER_SYS(tb, mesh, TB_MESH_SYS_PRIO)

typedef struct TbMeshShaderArgs {
  TbRenderSystem *rnd_sys;
  VkFormat depth_format;
  VkFormat color_format;
  VkPipelineLayout pipe_layout;
} TbMeshShaderArgs;

VkPipeline create_prepass_pipeline(void *args) {
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
    create_info.codeSize = sizeof(opaque_prepass_vert);
    create_info.pCode = (const uint32_t *)opaque_prepass_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Vert",
                         &vert_mod);

    create_info.codeSize = sizeof(opaque_prepass_frag);
    create_info.pCode = (const uint32_t *)opaque_prepass_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Opaque Prepass Frag",
                         &frag_mod);
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
        .codeSize = sizeof(gltf_vert),
        .pCode = (const uint32_t *)gltf_vert,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Vert", &vert_mod);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_frag),
        .pCode = (const uint32_t *)gltf_frag,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag", &frag_mod);
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
        .codeSize = sizeof(gltf_vert),
        .pCode = (const uint32_t *)gltf_vert,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Vert", &vert_mod);
  }
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(gltf_frag),
        .pCode = (const uint32_t *)gltf_frag,
    };
    tb_rnd_create_shader(rnd_sys, &create_info, "GLTF Frag", &frag_mod);
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

TbMeshSystem create_mesh_system_internal(ecs_world_t *ecs, TbAllocator gp_alloc,
                                         TbAllocator tmp_alloc,
                                         TbRenderSystem *rnd_sys,
                                         TbViewSystem *view_sys,
                                         TbRenderObjectSystem *ro_sys,
                                         TbRenderPipelineSystem *rp_sys) {
  TbMeshSystem sys = {
      .gp_alloc = gp_alloc,
      .tmp_alloc = tmp_alloc,
      .rnd_sys = rnd_sys,
      .view_sys = view_sys,
      .render_object_system = ro_sys,
      .rp_sys = rp_sys,
  };
  TB_DYN_ARR_RESET(sys.meshes, gp_alloc, 8);
  TbRenderPassId prepass_id = rp_sys->opaque_depth_normal_pass;
  TbRenderPassId opaque_pass_id = rp_sys->opaque_color_pass;
  TbRenderPassId transparent_pass_id = rp_sys->transparent_color_pass;

  tb_auto mesh_set_layout = tb_mesh_sys_get_set_layout(ecs);

  // Setup mesh system for rendering
  {
    VkResult err = VK_SUCCESS;

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
      VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

      TbMeshShaderArgs args = {
          .rnd_sys = rnd_sys,
          .depth_format = depth_format,
          .pipe_layout = sys.prepass_layout,
      };
      sys.prepass_shader = tb_shader_load(ecs, create_prepass_pipeline, &args,
                                          sizeof(TbMeshShaderArgs));
    }

    // Create pipeline layouts
    {
      const uint32_t layout_count = 12;
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = layout_count,
          .pSetLayouts =
              (VkDescriptorSetLayout[layout_count]){
                  view_sys->set_layout,
                  tb_mat_sys_get_set_layout(ecs),
                  sys.draw_set_layout,
                  ro_sys->set_layout,
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

void destroy_mesh_system(ecs_world_t *ecs, TbMeshSystem *self) {
  TbRenderSystem *rnd_sys = self->rnd_sys;

  tb_shader_destroy(ecs, self->opaque_shader);
  tb_shader_destroy(ecs, self->transparent_shader);
  tb_shader_destroy(ecs, self->prepass_shader);
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

void mesh_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Mesh Draw Tick", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT_DEFINE(ecs, TbMeshSystem);

  tb_auto mesh_sys = ecs_field(it, TbMeshSystem, 1);
  tb_auto ro_sys = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  // If any shaders aren't ready just bail
  if (!tb_is_shader_ready(ecs, mesh_sys->opaque_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->transparent_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->prepass_shader)) {
    TracyCZoneEnd(ctx);
    return;
  }

  // For each camera
  tb_auto camera_it = ecs_query_iter(ecs, mesh_sys->camera_query);
  while (ecs_query_next(&camera_it)) {
    tb_auto cameras = ecs_field(&camera_it, TbCameraComponent, 1);
    for (int32_t cam_idx = 0; cam_idx < camera_it.count; ++cam_idx) {
      TracyCZoneN(cam_ctx, "Camera", 1);
      tb_auto camera = &cameras[cam_idx];
      tb_auto view_set =
          tb_view_system_get_descriptor(view_sys, camera->view_id);
      // Skip camera if view set isn't ready
      if (view_set == VK_NULL_HANDLE) {
        TracyCZoneEnd(cam_ctx);
        continue;
      }

      const float width = camera->width;
      const float height = camera->height;

      // Run query to determine how many meshes so we can pre-allocate space for
      // batches
      TracyCZoneN(count_ctx, "Count Meshes", true);
      tb_auto mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
      uint32_t opaque_draw_count = 0;
      uint32_t trans_draw_count = 0;
      while (ecs_query_next(&mesh_it)) {
        tb_auto meshes = ecs_field(&mesh_it, TbMeshComponent, 1);
        for (tb_auto mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
          TbMesh2 mesh = meshes[mesh_idx].mesh2;

          if (!tb_is_mesh_ready(it->world, mesh)) {
            continue;
          }

          tb_auto submesh_itr = ecs_children(it->world, mesh);

          uint32_t submesh_count = 0;
          while (ecs_children_next(&submesh_itr)) {
            submesh_count += submesh_itr.count;
          }

          submesh_itr = ecs_children(it->world, mesh);
          while (ecs_children_next(&submesh_itr)) {
            for (int32_t sm_i = 0; sm_i < submesh_itr.count; ++sm_i) {
              TbSubMesh2 sm_ent = submesh_itr.entities[sm_i];
              if (!ecs_has(it->world, sm_ent, TbSubMesh2Data)) {
                TB_CHECK(false,
                         "Submesh entity unexpectedly lacked submesh data");
                continue;
              }
              tb_auto sm = ecs_get(it->world, sm_ent, TbSubMesh2Data);

              // Material must be loaded and ready
              if (!tb_is_material_ready(ecs, sm->material)) {
                continue;
              }

              if (tb_is_mat_transparent(ecs, sm->material)) {
                trans_draw_count += submesh_count;
              } else {
                opaque_draw_count += submesh_count;
              }
            }
          }
        }
      }
      mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
      TracyCZoneEnd(count_ctx);

      const uint32_t max_draw_count = opaque_draw_count + trans_draw_count;
      if (max_draw_count == 0) {
        TracyCZoneEnd(cam_ctx);
        continue;
      }

      tb_auto obj_set = tb_render_object_sys_get_set(ro_sys);
      tb_auto tex_set = tb_tex_sys_get_set(ecs);
      tb_auto mat_set = tb_mat_sys_get_set(ecs);
      tb_auto idx_set = tb_mesh_sys_get_idx_set(ecs);
      tb_auto pos_set = tb_mesh_sys_get_pos_set(ecs);
      tb_auto norm_set = tb_mesh_sys_get_norm_set(ecs);
      tb_auto tan_set = tb_mesh_sys_get_tan_set(ecs);
      tb_auto uv0_set = tb_mesh_sys_get_uv0_set(ecs);

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
        tb_rnd_frame_desc_pool_tick(
            rnd_sys, "mesh_draw_instances", &create_info, layouts, NULL,
            mesh_sys->draw_pools.pools, set_count, set_count);
      }

      TracyCZoneN(ctx2, "Iterate Meshes", true);
      while (ecs_query_next(&mesh_it)) {
        tb_auto meshes = ecs_field(&mesh_it, TbMeshComponent, 1);
        tb_auto render_objects = ecs_field(&mesh_it, TbRenderObject, 2);
        for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
          tb_auto mesh = meshes[mesh_idx].mesh2;
          tb_auto ro = render_objects[mesh_idx];

          if (!tb_is_mesh_ready(it->world, mesh)) {
            continue;
          }

          tb_auto mesh_desc_idx = *ecs_get(it->world, mesh, TbMeshIndex);

          tb_auto submesh_itr = ecs_children(it->world, mesh);
          while (ecs_children_next(&submesh_itr)) {
            for (int32_t sm_i = 0; sm_i < submesh_itr.count; ++sm_i) {
              TbSubMesh2 sm_ent = submesh_itr.entities[sm_i];
              if (!ecs_has(it->world, sm_ent, TbSubMesh2Data)) {
                TB_CHECK(false,
                         "Submesh entity unexpectedly lacked submesh data");
                continue;
              }
              tb_auto sm = ecs_get(it->world, sm_ent, TbSubMesh2Data);
              // Material must be loaded and ready
              if (!tb_is_material_ready(ecs, sm->material)) {
                continue;
              }

              // Deduce whether to write to opaque or transparent data
              tb_auto draw_cmds = opaque_draw_cmds;
              tb_auto draw_count = &opaque_cmd_count;
              tb_auto draw_data = opaque_draw_data;
              if (tb_is_mat_transparent(ecs, sm->material)) {
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
                  .obj_idx = ro.index,
                  .mesh_idx = mesh_desc_idx,
                  .mat_idx = *ecs_get(ecs, sm->material, TbMaterialComponent),
                  .index_offset = sm->index_offset,
                  .vertex_offset = sm->vertex_offset,
              };
              (*draw_count) += 1;
            }
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
      TB_CHECK(mesh_sys->opaque_batch == NULL, "Opaque batch was not consumed");
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
          .pipeline = tb_shader_get_pipeline(ecs, mesh_sys->opaque_shader),
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
          .pipeline = tb_shader_get_pipeline(ecs, mesh_sys->transparent_shader),
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
        prepass_batch.pipeline =
            tb_shader_get_pipeline(ecs, mesh_sys->prepass_shader);
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

      TracyCZoneEnd(cam_ctx);
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_register_mesh_sys(TbWorld *world) {
  TracyCZoneNC(ctx, "Register Mesh Sys", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMeshSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto ro_sys = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  tb_auto rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);

  tb_auto sys =
      create_mesh_system_internal(ecs, world->gp_alloc, world->tmp_alloc,
                                  rnd_sys, view_sys, ro_sys, rp_sys);
  sys.camera_query = ecs_query(ecs, {.filter.terms = {
                                         {.id = ecs_id(TbCameraComponent)},
                                     }});
  sys.mesh_query = ecs_query(ecs, {.filter.terms = {
                                       {.id = ecs_id(TbMeshComponent)},
                                       {.id = ecs_id(TbRenderObject)},
                                   }});
  sys.dir_light_query =
      ecs_query(ecs, {.filter.terms = {
                          {.id = ecs_id(TbDirectionalLightComponent)},
                      }});

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbMeshSystem), TbMeshSystem, &sys);

  ECS_SYSTEM(ecs, mesh_draw_tick, EcsOnStore, TbMeshSystem(TbMeshSystem));

  TracyCZoneEnd(ctx);
}

void tb_unregister_mesh_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  TbMeshSystem *sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  ecs_query_fini(sys->dir_light_query);
  ecs_query_fini(sys->mesh_query);
  ecs_query_fini(sys->camera_query);
  destroy_mesh_system(ecs, sys);
  ecs_singleton_remove(ecs, TbMeshSystem);
}
