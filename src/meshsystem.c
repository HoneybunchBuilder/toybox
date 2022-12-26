#include "meshsystem.h"

#include "cameracomponent.h"
#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "lightcomponent.h"
#include "materialsystem.h"
#include "meshcomponent.h"
#include "profiling.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "shadow.hlsli"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "vkdbg.h"
#include "world.h"

#include "meshoptimizer.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "depth_frag.h"
#include "depth_vert.h"
#include "gltf_P3N3T4U2_frag.h"
#include "gltf_P3N3T4U2_vert.h"
#include "gltf_P3N3U2_frag.h"
#include "gltf_P3N3U2_vert.h"
#include "gltf_P3N3_frag.h"
#include "gltf_P3N3_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

// We know how many input permutations we have
static const uint32_t max_pipe_count = VI_Count * GLTF_PERM_COUNT;

typedef struct SubMeshDraw {
  VkDescriptorSet mat_set;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint32_t vertex_binding_count;
  uint64_t vertex_binding_offsets[TB_VERTEX_BINDING_MAX];
} SubMeshDraw;

typedef struct MeshDraw {
  VkDescriptorSet obj_set;
  VkBuffer geom_buffer;
  uint32_t submesh_draw_count;
  SubMeshDraw submesh_draws[TB_SUBMESH_MAX];
} MeshDraw;

typedef struct MeshDrawView {
  VkViewport viewport;
  VkRect2D scissor;
  VkDescriptorSet view_set;
  uint32_t draw_count;
  MeshDraw *draws;
} MeshDrawView;

typedef struct MeshDrawBatch {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  uint32_t view_count;
  MeshDrawView *views;
} MeshDrawBatch;

typedef struct ShadowSubDraw {
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint64_t vertex_binding_offset;
} ShadowSubDraw;

typedef struct ShadowDraw {
  ShadowDrawConstants consts;
  VkBuffer geom_buffer;
  uint32_t submesh_draw_count;
  ShadowSubDraw submesh_draws[TB_SUBMESH_MAX];
} ShadowDraw;

typedef struct ShadowDrawView {
  VkViewport viewport;
  VkRect2D scissor;
  ShadowViewConstants consts;
  uint32_t draw_count;
  ShadowDraw *draws;
} ShadowDrawView;

typedef struct ShadowDrawBatch {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  uint32_t view_count;
  ShadowDrawView *views;
} ShadowDrawBatch;

typedef struct VisibleSet {
  TbViewId view;
  uint32_t mesh_count;
  MeshComponent const **meshes;
} VisibleSet;

VkResult create_shadow_pipeline(RenderSystem *render_system, VkRenderPass pass,
                                VkPipelineLayout pipe_layout,
                                VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(depth_vert);
    create_info.pCode = (const uint32_t *)depth_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Shadow Vert",
                               &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow vert shader module", err);

    create_info.codeSize = sizeof(depth_frag);
    create_info.pCode = (const uint32_t *)depth_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Shadow Frag",
                               &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow frag shader module", err);
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
                      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX}},
              .vertexAttributeDescriptionCount = 1,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[1]){
                      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0}},
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
              .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
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
                                         "Shadow Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create shadow pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_mesh_pipelines(RenderSystem *render_system, Allocator tmp_alloc,
                               Allocator std_alloc, VkRenderPass pass,
                               VkPipelineLayout pipe_layout,
                               uint32_t *pipe_count, VkPipeline **pipelines) {
  VkResult err = VK_SUCCESS;

  // VI 1: Position & Normal - P3N3
  // VI 2: Position & Normal & Texcoord0 - P3N3U2
  // VI 3: Position & Normal & Tangent & Texcoord0 - P3N3T4U2

  VkVertexInputBindingDescription vert_bindings_P3N3[2] = {
      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputBindingDescription vert_bindings_P3N3U2[3] = {
      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
      {2, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputBindingDescription vert_bindings_P3N3T4U2[4] = {
      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
      {2, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
      {3, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputAttributeDescription vert_attrs_P3N3[2] = {
      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
      {1, 1, VK_FORMAT_R8G8B8A8_SNORM, 0},
  };

  VkVertexInputAttributeDescription vert_attrs_P3N3U2[3] = {
      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
      {1, 1, VK_FORMAT_R8G8B8A8_SNORM, 0},
      {2, 2, VK_FORMAT_R16G16_SINT, 0},
  };

  VkVertexInputAttributeDescription vert_attrs_P3N3T4U2[4] = {
      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
      {1, 1, VK_FORMAT_R8G8B8A8_SNORM, 0},
      {2, 2, VK_FORMAT_R8G8B8A8_SNORM, 0},
      {3, 3, VK_FORMAT_R16G16_SINT, 0},
  };

  VkPipelineVertexInputStateCreateInfo vert_input_state_P3N3 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 2,
      .pVertexBindingDescriptions = vert_bindings_P3N3,
      .vertexAttributeDescriptionCount = 2,
      .pVertexAttributeDescriptions = vert_attrs_P3N3,
  };

  VkPipelineVertexInputStateCreateInfo vert_input_state_P3N3U2 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 3,
      .pVertexBindingDescriptions = vert_bindings_P3N3U2,
      .vertexAttributeDescriptionCount = 3,
      .pVertexAttributeDescriptions = vert_attrs_P3N3U2,
  };

  VkPipelineVertexInputStateCreateInfo vert_input_state_P3N3T4U2 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 4,
      .pVertexBindingDescriptions = vert_bindings_P3N3T4U2,
      .vertexAttributeDescriptionCount = 4,
      .pVertexAttributeDescriptions = vert_attrs_P3N3T4U2,
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
  VkPipelineDepthStencilStateCreateInfo depth_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_GREATER,
      .maxDepthBounds = 1.0f,
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
#define DYN_STATE_COUNT 2
  VkDynamicState dyn_states[DYN_STATE_COUNT] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = DYN_STATE_COUNT,
      .pDynamicStates = dyn_states,
  };
#undef DYN_STATE_COUNT

  // Load Shader Modules
  VkShaderModule vert_mod_P3N3 = VK_NULL_HANDLE;
  VkShaderModule frag_mod_P3N3 = VK_NULL_HANDLE;
  VkShaderModule vert_mod_P3N3U2 = VK_NULL_HANDLE;
  VkShaderModule frag_mod_P3N3U2 = VK_NULL_HANDLE;
  VkShaderModule vert_mod_P3N3T4U2 = VK_NULL_HANDLE;
  VkShaderModule frag_mod_P3N3T4U2 = VK_NULL_HANDLE;

  VkShaderModuleCreateInfo shader_mod_create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
  };
  shader_mod_create_info.codeSize = sizeof(gltf_P3N3_vert);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_P3N3_vert;
  err = tb_rnd_create_shader(render_system, &shader_mod_create_info,
                             "P3N3 Vert", &vert_mod_P3N3);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);

  shader_mod_create_info.codeSize = sizeof(gltf_P3N3_frag);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_P3N3_frag;
  err = tb_rnd_create_shader(render_system, &shader_mod_create_info,
                             "P3N3 Frag", &frag_mod_P3N3);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);

  shader_mod_create_info.codeSize = sizeof(gltf_P3N3U2_vert);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_P3N3U2_vert;
  err = tb_rnd_create_shader(render_system, &shader_mod_create_info,
                             "P3N3U2 Vert", &vert_mod_P3N3U2);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);

  shader_mod_create_info.codeSize = sizeof(gltf_P3N3U2_frag);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_P3N3U2_frag;
  err = tb_rnd_create_shader(render_system, &shader_mod_create_info,
                             "P3N3U2 Frag", &frag_mod_P3N3U2);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);

  shader_mod_create_info.codeSize = sizeof(gltf_P3N3T4U2_vert);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_P3N3T4U2_vert;
  err = tb_rnd_create_shader(render_system, &shader_mod_create_info,
                             "P3N3T4U2 Vert", &vert_mod_P3N3T4U2);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);

  shader_mod_create_info.codeSize = sizeof(gltf_P3N3T4U2_frag);
  shader_mod_create_info.pCode = (const uint32_t *)gltf_P3N3T4U2_frag;
  err = tb_rnd_create_shader(render_system, &shader_mod_create_info,
                             "P3N3T4U2 Frag", &frag_mod_P3N3T4U2);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);

  VkPipelineShaderStageCreateInfo vert_stage_P3N3 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_mod_P3N3,
      .pName = "vert",
  };
  VkPipelineShaderStageCreateInfo frag_stage_P3N3 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_mod_P3N3,
      .pName = "frag",
  };
  VkPipelineShaderStageCreateInfo vert_stage_P3N3U2 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_mod_P3N3U2,
      .pName = "vert",
  };
  VkPipelineShaderStageCreateInfo frag_stage_P3N3U2 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_mod_P3N3U2,
      .pName = "frag",
  };
  VkPipelineShaderStageCreateInfo vert_stage_P3N3T4U2 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_mod_P3N3T4U2,
      .pName = "vert",
  };
  VkPipelineShaderStageCreateInfo frag_stage_P3N3T4U2 = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_mod_P3N3T4U2,
      .pName = "frag",
  };

  VkPipelineShaderStageCreateInfo stages_P3N3[2] = {vert_stage_P3N3,
                                                    frag_stage_P3N3};

  VkPipelineShaderStageCreateInfo stages_P3N3U2[2] = {vert_stage_P3N3U2,
                                                      frag_stage_P3N3U2};

  VkPipelineShaderStageCreateInfo stages_P3N3T4U2[2] = {vert_stage_P3N3T4U2,
                                                        frag_stage_P3N3T4U2};

  VkGraphicsPipelineCreateInfo create_info_base = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pInputAssemblyState = &input_assembly_state,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster_state,
      .pMultisampleState = &multisample_state,
      .pDepthStencilState = &depth_state,
      .pColorBlendState = &color_blend_state,
      .pDynamicState = &dynamic_state,
      .layout = pipe_layout,
      .renderPass = pass,
  };

  VkGraphicsPipelineCreateInfo create_info_bases[VI_Count] = {0};
  create_info_bases[0] = create_info_base;
  create_info_bases[0].pStages = stages_P3N3;
  create_info_bases[0].pVertexInputState = &vert_input_state_P3N3;
  create_info_bases[1] = create_info_base;
  create_info_bases[1].pStages = stages_P3N3U2;
  create_info_bases[1].pVertexInputState = &vert_input_state_P3N3U2;
  create_info_bases[2] = create_info_base;
  create_info_bases[2].pStages = stages_P3N3T4U2;
  create_info_bases[2].pVertexInputState = &vert_input_state_P3N3T4U2;

  // Create pipelines
  {
    VkGraphicsPipelineCreateInfo *create_info =
        tb_alloc_nm_tp(tmp_alloc, max_pipe_count, VkGraphicsPipelineCreateInfo);
    VkPipeline *pipes = tb_alloc_nm_tp(std_alloc, max_pipe_count, VkPipeline);

    uint32_t perm_idx = 0;
    for (uint32_t vi_idx = 0; vi_idx < VI_Count; ++vi_idx) {
      const VkGraphicsPipelineCreateInfo *base = &create_info_bases[vi_idx];

      const uint32_t stage_count = base->stageCount;
      const uint32_t perm_stage_count = GLTF_PERM_COUNT * stage_count;

      // Every shader stage needs its own create info
      VkPipelineShaderStageCreateInfo *pipe_stage_info = tb_alloc_nm_tp(
          tmp_alloc, perm_stage_count, VkPipelineShaderStageCreateInfo);

      VkSpecializationMapEntry map_entries[1] = {
          {0, 0, sizeof(uint32_t)},
      };

      VkSpecializationInfo *spec_info =
          tb_alloc_nm_tp(tmp_alloc, GLTF_PERM_COUNT, VkSpecializationInfo);
      uint32_t *flags = tb_alloc_nm_tp(tmp_alloc, GLTF_PERM_COUNT, uint32_t);

      // Insert specialization info to every shader stage
      for (uint32_t fp_idx = 0; fp_idx < GLTF_PERM_COUNT; ++fp_idx) {

        create_info[perm_idx] = *base;

        flags[fp_idx] = fp_idx;
        spec_info[fp_idx] = (VkSpecializationInfo){
            1,
            map_entries,
            sizeof(uint32_t),
            &flags[fp_idx],
        };

        uint32_t stage_idx = fp_idx * stage_count;
        for (uint32_t i = 0; i < stage_count; ++i) {
          VkPipelineShaderStageCreateInfo *stage =
              &pipe_stage_info[stage_idx + i];
          *stage = base->pStages[i];
          stage->pSpecializationInfo = &spec_info[fp_idx];
        }
        create_info[perm_idx].pStages = &pipe_stage_info[stage_idx];

        // Set permutation tracking values
        // pipe->input_flags[perm_idx] = vertex_input;
        // pipe->pipeline_flags[perm_idx] = fp_idx;
        perm_idx++;
      }
    }
    err = tb_rnd_create_graphics_pipelines(render_system, max_pipe_count,
                                           create_info, "Mesh Pipeline", pipes);
    TB_VK_CHECK_RET(err, "Failed to create graphics pipelines", err);

    *pipelines = pipes;
    *pipe_count = max_pipe_count;
  }

  // Can destroy shader moduless
  tb_rnd_destroy_shader(render_system, vert_mod_P3N3);
  tb_rnd_destroy_shader(render_system, frag_mod_P3N3);
  tb_rnd_destroy_shader(render_system, vert_mod_P3N3U2);
  tb_rnd_destroy_shader(render_system, frag_mod_P3N3U2);
  tb_rnd_destroy_shader(render_system, vert_mod_P3N3T4U2);
  tb_rnd_destroy_shader(render_system, frag_mod_P3N3T4U2);

  return err;
}

void shadow_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Mesh Shadow Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Shadows", 1, true);

  const ShadowDrawBatch *shadow_batches = (const ShadowDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    const ShadowDrawBatch *batch = &shadow_batches[batch_idx];
    if (batch->view_count == 0) {
      TracyCZoneEnd(batch_ctx);
      continue;
    }

    TracyCVkNamedZone(gpu_ctx, batch_scope, buffer, "Batch", 2, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    for (uint32_t view_idx = 0; view_idx < batch->view_count; ++view_idx) {
      TracyCZoneNC(view_ctx, "View", TracyCategoryColorRendering, true);
      const ShadowDrawView *view = &batch->views[view_idx];
      if (view->draw_count == 0) {
        TracyCZoneEnd(view_ctx);
        continue;
      }
      TracyCVkNamedZone(gpu_ctx, view_scope, buffer, "View", 3, true);
      cmd_begin_label(buffer, "View", (float4){0.0f, 0.0f, 0.6f, 1.0f});
      vkCmdSetViewport(buffer, 0, 1, &view->viewport);
      vkCmdSetScissor(buffer, 0, 1, &view->scissor);

      for (uint32_t draw_idx = 0; draw_idx < view->draw_count; ++draw_idx) {
        TracyCZoneNC(draw_ctx, "Draw", TracyCategoryColorRendering, true);
        const ShadowDraw *draw = &view->draws[draw_idx];
        if (draw->submesh_draw_count == 0) {
          TracyCZoneEnd(draw_ctx);
          continue;
        }
        TracyCVkNamedZone(gpu_ctx, mesh_scope, buffer, "Mesh", 4, true);
        cmd_begin_label(buffer, "Mesh", (float4){0.0f, 0.0f, 0.4f, 1.0f});
        VkBuffer geom_buffer = draw->geom_buffer;

        ShadowConstants consts = {
            .vp = view->consts.vp,
            .m = draw->consts.m,
        };
        vkCmdPushConstants(buffer, batch->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(ShadowConstants), &consts);

        for (uint32_t sub_idx = 0; sub_idx < draw->submesh_draw_count;
             ++sub_idx) {
          TracyCZoneNC(submesh_ctx, "Submesh", TracyCategoryColorRendering,
                       true);
          const ShadowSubDraw *submesh = &draw->submesh_draws[sub_idx];
          if (submesh->index_count > 0) {
            TracyCVkNamedZone(gpu_ctx, submesh_scope, buffer, "Submesh", 5,
                              true);
            vkCmdBindIndexBuffer(buffer, geom_buffer, submesh->index_offset,
                                 submesh->index_type);
            vkCmdBindVertexBuffers(buffer, 0, 1, &geom_buffer,
                                   &submesh->vertex_binding_offset);

            vkCmdDrawIndexed(buffer, submesh->index_count, 1, 0, 0, 0);
            TracyCVkZoneEnd(submesh_scope);
          }
          TracyCZoneEnd(submesh_ctx);
        }
        cmd_end_label(buffer);
        TracyCVkZoneEnd(mesh_scope);
        TracyCZoneEnd(draw_ctx);
      }
      cmd_end_label(buffer);
      TracyCVkZoneEnd(view_scope);
      TracyCZoneEnd(view_ctx);
    }

    cmd_end_label(buffer);
    TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }

  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void opaque_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Meshes", 1, true);

  const MeshDrawBatch *mesh_batches = (const MeshDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    const MeshDrawBatch *batch = &mesh_batches[batch_idx];
    if (batch->view_count == 0) {
      TracyCZoneEnd(batch_ctx);
      continue;
    }

    TracyCVkNamedZone(gpu_ctx, batch_scope, buffer, "Batch", 2, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    for (uint32_t view_idx = 0; view_idx < batch->view_count; ++view_idx) {
      TracyCZoneNC(view_ctx, "View", TracyCategoryColorRendering, true);
      const MeshDrawView *view = &batch->views[view_idx];
      if (view->draw_count == 0) {
        TracyCZoneEnd(view_ctx);
        continue;
      }
      TracyCVkNamedZone(gpu_ctx, view_scope, buffer, "View", 3, true);
      cmd_begin_label(buffer, "View", (float4){0.0f, 0.0f, 0.6f, 1.0f});
      vkCmdSetViewport(buffer, 0, 1, &view->viewport);
      vkCmdSetScissor(buffer, 0, 1, &view->scissor);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              2, 1, &view->view_set, 0, NULL);
      for (uint32_t draw_idx = 0; draw_idx < view->draw_count; ++draw_idx) {
        TracyCZoneNC(draw_ctx, "Draw", TracyCategoryColorRendering, true);
        const MeshDraw *draw = &view->draws[draw_idx];
        if (draw->submesh_draw_count == 0) {
          TracyCZoneEnd(draw_ctx);
          continue;
        }
        TracyCVkNamedZone(gpu_ctx, mesh_scope, buffer, "Mesh", 4, true);
        cmd_begin_label(buffer, "Mesh", (float4){0.0f, 0.0f, 0.4f, 1.0f});
        vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                1, 1, &draw->obj_set, 0, NULL);
        VkBuffer geom_buffer = draw->geom_buffer;

        for (uint32_t sub_idx = 0; sub_idx < draw->submesh_draw_count;
             ++sub_idx) {
          TracyCZoneNC(submesh_ctx, "Submesh", TracyCategoryColorRendering,
                       true);
          const SubMeshDraw *submesh = &draw->submesh_draws[sub_idx];
          if (submesh->index_count > 0) {
            TracyCVkNamedZone(gpu_ctx, submesh_scope, buffer, "Submesh", 5,
                              true);
            vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    layout, 0, 1, &submesh->mat_set, 0, NULL);
            vkCmdBindIndexBuffer(buffer, geom_buffer, submesh->index_offset,
                                 submesh->index_type);
            for (uint32_t vb_idx = 0; vb_idx < submesh->vertex_binding_count;
                 ++vb_idx) {
              vkCmdBindVertexBuffers(buffer, vb_idx, 1, &geom_buffer,
                                     &submesh->vertex_binding_offsets[vb_idx]);
            }

            vkCmdDrawIndexed(buffer, submesh->index_count, 1, 0, 0, 0);
            TracyCVkZoneEnd(submesh_scope);
          }
          TracyCZoneEnd(submesh_ctx);
        }
        cmd_end_label(buffer);
        TracyCVkZoneEnd(mesh_scope);
        TracyCZoneEnd(draw_ctx);
      }
      cmd_end_label(buffer);
      TracyCVkZoneEnd(view_scope);
      TracyCZoneEnd(view_ctx);
    }

    cmd_end_label(buffer);
    TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }

  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

bool create_mesh_system(MeshSystem *self, const MeshSystemDescriptor *desc,
                        uint32_t system_dep_count, System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which meshes depend on", false);
  MaterialSystem *material_system =
      tb_get_system(system_deps, system_dep_count, MaterialSystem);
  TB_CHECK_RETURN(material_system,
                  "Failed to find material system which meshes depend on",
                  false);
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  TB_CHECK_RETURN(view_system,
                  "Failed to find view system which meshes depend on", false);
  RenderObjectSystem *render_object_system =
      tb_get_system(system_deps, system_dep_count, RenderObjectSystem);
  TB_CHECK_RETURN(render_object_system,
                  "Failed to find render object system which meshes depend on",
                  false);
  RenderPipelineSystem *render_pipe_system =
      tb_get_system(system_deps, system_dep_count, RenderPipelineSystem);
  TB_CHECK_RETURN(
      render_pipe_system,
      "Failed to find render pipeline system which meshes depend on", false);

  *self = (MeshSystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .render_system = render_system,
      .material_system = material_system,
      .view_system = view_system,
      .render_object_system = render_object_system,
      .render_pipe_system = render_pipe_system,
  };

  TbRenderPassId opaque_pass_id = self->render_pipe_system->opaque_color_pass;
  const TbRenderPassId *shadow_pass_ids =
      self->render_pipe_system->shadow_passes;

  // Setup mesh system for rendering
  {
    VkResult err = VK_SUCCESS;

    // Look up passes
    VkRenderPass opaque_pass =
        tb_render_pipeline_get_pass(self->render_pipe_system, opaque_pass_id);
    VkRenderPass shadow_pass = tb_render_pipeline_get_pass(
        self->render_pipe_system, shadow_pass_ids[0]);

    // Get descriptor set layouts from related systems
    {
      self->obj_set_layout = render_object_system->set_layout;
      self->view_set_layout = view_system->set_layout;
    }

    // Create pipeline layout
    {
#define LAYOUT_COUNT 3
      VkDescriptorSetLayout layouts[LAYOUT_COUNT] = {
          material_system->set_layout,
          self->obj_set_layout,
          self->view_set_layout,
      };

      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = LAYOUT_COUNT,
          .pSetLayouts = layouts,
      };
#undef LAYOUT_COUNT
      err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                          "GLTF Pipeline Layout",
                                          &self->pipe_layout);
    }

    err = create_mesh_pipelines(self->render_system, self->tmp_alloc,
                                self->std_alloc, opaque_pass, self->pipe_layout,
                                &self->pipe_count, &self->pipelines);
    TB_VK_CHECK_RET(err, "Failed to create mesh pipelines", false);

    {
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .pushConstantRangeCount = 1,
          .pPushConstantRanges =
              (VkPushConstantRange[1]){
                  {
                      .size = sizeof(ShadowConstants),
                      .offset = 0,
                      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                  },
              },
      };
      err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                          "Shadow Pipeline Layout",
                                          &self->shadow_pipe_layout);
      TB_VK_CHECK_RET(err, "Failed to create shadow pipeline layout", false);
    }

    err = create_shadow_pipeline(self->render_system, shadow_pass,
                                 self->shadow_pipe_layout,
                                 &self->shadow_pipeline);
    TB_VK_CHECK_RET(err, "Failed to create shadow pipeline", false);
  }

  // Register drawing with the pipeline
  self->opaque_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(MeshDrawBatch),
                              .draw_fn = opaque_pass_record,
                              .pass_id = opaque_pass_id,
                          });
  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    self->shadow_draw_ctxs[i] = tb_render_pipeline_register_draw_context(
        render_pipe_system, &(DrawContextDescriptor){
                                .batch_size = sizeof(MeshDrawBatch),
                                .draw_fn = shadow_pass_record,
                                .pass_id = render_pipe_system->shadow_passes[i],
                            });
  }

  return true;
}

void destroy_mesh_system(MeshSystem *self) {
  RenderSystem *render_system = self->render_system;

  tb_rnd_destroy_pipeline(render_system, self->shadow_pipeline);
  for (uint32_t i = 0; i < self->pipe_count; ++i) {
    tb_rnd_destroy_pipeline(render_system, self->pipelines[i]);
  }

  tb_rnd_destroy_pipe_layout(render_system, self->shadow_pipe_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);

  for (uint32_t i = 0; i < self->mesh_count; ++i) {
    if (self->mesh_ref_counts[i] != 0) {
      TB_CHECK(false, "Leaking meshes");
    }
  }

  *self = (MeshSystem){0};
}

uint32_t get_pipeline_for_input_and_mat(MeshSystem *self, TbVertexInput input,
                                        TbMaterialPerm mat) {
  TracyCZone(ctx, true);
  // We know the layout of the distribution of pipelines so we can
  // decode the vertex input and the material permutation from the
  // index
  for (uint32_t pipe_idx = 0; pipe_idx < self->pipe_count; ++pipe_idx) {
    const TbVertexInput vi = pipe_idx / GLTF_PERM_COUNT;
    const TbMaterialPerm mp = pipe_idx % GLTF_PERM_COUNT;

    if (input == vi && mat == mp) {
      TracyCZoneEnd(ctx);
      return pipe_idx;
    }
  }
  TracyCZoneEnd(ctx);
  TB_CHECK_RETURN(false, "Failed to find pipeline for given mesh permutations",
                  SDL_MAX_UINT32);
}

void tick_mesh_system(MeshSystem *self, const SystemInput *input,
                      SystemOutput *output, float delta_seconds) {
  (void)delta_seconds;

  TracyCZoneNC(ctx, "Mesh System", TracyCategoryColorRendering, true);

  const uint32_t camera_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *camera_store =
      tb_get_column_check_id(input, 0, 0, CameraComponentId);

  const uint32_t dir_light_count = tb_get_column_component_count(input, 1);
  const PackedComponentStore *dir_light_store =
      tb_get_column_check_id(input, 1, 0, DirectionalLightComponentId);

  const uint32_t mesh_count = tb_get_column_component_count(input, 2);
  const PackedComponentStore *mesh_store =
      tb_get_column_check_id(input, 2, 0, MeshComponentId);
  const PackedComponentStore *mesh_transform_store =
      tb_get_column_check_id(input, 2, 1, TransformComponentId);

  if (mesh_count == 0 || camera_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  TB_PROF_MESSAGE("Camera Count: %d", camera_count);
  TB_PROF_MESSAGE("Directional Light Count: %d", dir_light_count);

  // Since we want to update the world matrix dirty flag on the transform
  // component, we need to write the components back out
  EntityId *out_entity_ids = tb_get_column_entity_ids(input, 2);
  TransformComponent *out_trans =
      tb_alloc_nm_tp(self->tmp_alloc, mesh_count, TransformComponent);
  SDL_memcpy(out_trans, mesh_transform_store->components,
             mesh_count * sizeof(TransformComponent));

  // Update each mesh's render object data while also collecting world space
  // AABBs for culling later
  AABB *world_space_aabbs = tb_alloc_nm_tp(self->tmp_alloc, mesh_count, AABB);
  {
    TracyCZoneN(ctx, "Calc World Space AABBs", true);
    for (uint32_t mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
      const MeshComponent *mesh_comp =
          tb_get_component(mesh_store, mesh_idx, MeshComponent);

      // Convert the transform to a matrix for rendering
      CommonObjectData data = {.m = {.row0 = {0}}};
      tb_transform_get_world_matrix(&out_trans[mesh_idx], &data.m);

      // Transform local aabb into world space
      AABB aabb = aabb_init();
      {
        float4 min = f3tof4(mesh_comp->local_aabb.min, 1.0f);
        float4 max = f3tof4(mesh_comp->local_aabb.max, 1.0f);

        min = mul4f44f(min, data.m);
        max = mul4f44f(max, data.m);

        aabb_add_point(&aabb, f4tof3(min));
        aabb_add_point(&aabb, f4tof3(max));
      }
      world_space_aabbs[mesh_idx] = aabb;

      tb_render_object_system_set_object_data(self->render_object_system,
                                              mesh_comp->object_id, &data);
    }
    TracyCZoneEnd(ctx);
  }

  // Figure out which meshes are visible
  // Worst case *everything* is visible so alloc for that off the tmp allocator
  VisibleSet *visible_sets =
      tb_alloc_nm_tp(self->tmp_alloc, camera_count, VisibleSet);
  {
    TracyCZoneN(ctx, "View Culling", true);
    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      const CameraComponent *camera =
          tb_get_component(camera_store, cam_idx, CameraComponent);

      visible_sets[cam_idx] = (VisibleSet){
          .view = camera->view_id,
          .meshes = tb_alloc_nm_tp(self->tmp_alloc, mesh_count,
                                   const MeshComponent *),
      };
    }

    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      VisibleSet *visible_set = &visible_sets[cam_idx];

      const View *view = tb_get_view(self->view_system, visible_set->view);
      const Frustum *frustum = &view->frustum;

      for (uint32_t mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
        // If the mesh's AABB isn't viewed by the frustum, don't issue this draw
        const AABB *world_aabb = &world_space_aabbs[mesh_idx];
        if (!frustum_test_aabb(frustum, world_aabb)) {
          continue;
        }
        const MeshComponent *mesh_comp =
            tb_get_component(mesh_store, mesh_idx, MeshComponent);

        const uint32_t idx = visible_set->mesh_count;
        visible_set->meshes[idx] = mesh_comp;
        visible_set->mesh_count++;
      }

      TB_PROF_MESSAGE("Visible Mesh Count: %d", visible_set->mesh_count);
    }
    TracyCZoneEnd(ctx);
  }

  VisibleSet *lit_sets =
      tb_alloc_nm_tp(self->tmp_alloc, TB_CASCADE_COUNT, VisibleSet);
  {
    TracyCZoneN(ctx, "View Culling", true);
    const DirectionalLightComponent *light =
        tb_get_component(dir_light_store, 0, DirectionalLightComponent);
    for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
         ++cascade_idx) {

      lit_sets[cascade_idx] = (VisibleSet){
          .view = light->cascade_views[cascade_idx],
          .meshes = tb_alloc_nm_tp(self->tmp_alloc, mesh_count,
                                   const MeshComponent *),
      };
    }

    for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
         ++cascade_idx) {
      VisibleSet *lit_set = &lit_sets[cascade_idx];

      const View *view = tb_get_view(self->view_system, lit_set->view);
      const Frustum *frustum = &view->frustum;

      for (uint32_t mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
        // If the mesh's AABB isn't viewed by the frustum, don't issue this draw
        const AABB *world_aabb = &world_space_aabbs[mesh_idx];
        if (!frustum_test_aabb(frustum, world_aabb)) {
          continue;
        }
        const MeshComponent *mesh_comp =
            tb_get_component(mesh_store, mesh_idx, MeshComponent);

        const uint32_t idx = lit_set->mesh_count;
        lit_set->meshes[idx] = mesh_comp;
        lit_set->mesh_count++;
      }

      TB_PROF_MESSAGE("Lit Mesh Count: %d", lit_set->mesh_count);
    }
    TracyCZoneEnd(ctx);
  }

  Allocator tmp_alloc = self->render_system->render_thread
                            ->frame_states[self->render_system->frame_idx]
                            .tmp_alloc.alloc;

  // Figure out # of unique pipelines so we know how many batches we have
  uint32_t pipe_count = 0;
  uint32_t *pipe_idxs = tb_alloc_nm_tp(tmp_alloc, max_pipe_count, uint32_t);
  SDL_memset(pipe_idxs, 0, sizeof(uint32_t) * max_pipe_count);
  {
    TracyCZoneN(ctx, "Batch Culling", true);
    for (uint32_t view_idx = 0; view_idx < camera_count; ++view_idx) {
      const VisibleSet *visible_set = &visible_sets[view_idx];
      for (uint32_t mesh_idx = 0; mesh_idx < visible_set->mesh_count;
           ++mesh_idx) {
        const MeshComponent *mesh_comp = visible_set->meshes[mesh_idx];
        for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
             ++sub_idx) {
          const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];
          TbMaterialPerm mat_perm =
              tb_mat_system_get_perm(self->material_system, submesh->material);
          uint32_t pipe_idx = get_pipeline_for_input_and_mat(
              self, submesh->vertex_input, mat_perm);

          if (pipe_idxs[pipe_idx] == 0) {
            pipe_idxs[pipe_idx] = pipe_count;
            pipe_count++;
            TB_CHECK(pipe_count <= max_pipe_count, "Unexpected # of pipelines");
          }
        }
      }
    }
    TracyCZoneEnd(ctx);
  }

  // Just collect a batch for every known used pipeline
  const uint32_t batch_count = pipe_count;

  TB_PROF_MESSAGE("Batch Count: %d", batch_count);

  // Allocate and initialize each batch
  MeshDrawBatch *batches = NULL;
  {
    TracyCZoneN(batch_ctx, "Allocate Batches", true);
    batches = tb_alloc_nm_tp(tmp_alloc, batch_count, MeshDrawBatch);
    for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
      // Each batch could use each view
      MeshDrawBatch *batch = &batches[batch_idx];
      *batch = (MeshDrawBatch){0};
      batch->views = tb_alloc_nm_tp(tmp_alloc, camera_count, MeshDrawView);
      batch->view_count = 0;

      for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
        MeshDrawView *view = &batch->views[cam_idx];
        // Each view already knows how many meshes it should see
        // Each mesh could have TB_SUBMESH_MAX # of submeshes
        *view = (MeshDrawView){0};
        view->draws = tb_alloc_nm_tp(
            tmp_alloc, visible_sets[cam_idx].mesh_count, MeshDraw);
        view->draw_count = 0;

        SDL_memset(view->draws, 0,
                   sizeof(MeshDraw) * visible_sets[cam_idx].mesh_count);
      }
    }
    TracyCZoneEnd(batch_ctx);
  }

  // Create one batch for each shadow cascade
  ShadowDrawBatch shadow_batches[TB_CASCADE_COUNT] = {0};
  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    // Batch could use each light
    shadow_batches[i].views =
        tb_alloc_nm_tp(tmp_alloc, dir_light_count, ShadowDrawView);
    shadow_batches[i].view_count = 0;

    ShadowDrawView *view = &shadow_batches[i].views[0];
    // Each view already knows how many meshes it should see
    // Each mesh could have TB_SUBMESH_MAX # of submeshes
    *view = (ShadowDrawView){0};
    view->draws = tb_alloc_nm_tp(tmp_alloc, lit_sets[i].mesh_count, ShadowDraw);
    view->draw_count = 0;

    SDL_memset(view->draws, 0, sizeof(ShadowDraw) * lit_sets[i].mesh_count);
  }

  // TODO: Make this less hacky
  const uint32_t width = self->render_system->render_thread->swapchain.width;
  const uint32_t height = self->render_system->render_thread->swapchain.height;

  for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
    const VisibleSet *visible_set = &visible_sets[cam_idx];

    // Get camera descriptor set
    VkDescriptorSet view_set =
        tb_view_system_get_descriptor(self->view_system, visible_set->view);

    for (uint32_t mesh_idx = 0; mesh_idx < visible_set->mesh_count;
         ++mesh_idx) {
      const MeshComponent *mesh_comp = visible_set->meshes[mesh_idx];

      // Get mesh descriptor set
      VkDescriptorSet obj_set = tb_render_object_system_get_descriptor(
          self->render_object_system, mesh_comp->object_id);

      // Organize draws into batches
      {
        // Determine which pipeline is in use
        VkBuffer geom_buffer =
            tb_mesh_system_get_gpu_mesh(self, mesh_comp->mesh_id);

        uint32_t submesh_draw_idx = 0;

        for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
             ++sub_idx) {
          const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];

          TbMaterialPerm mat_perm =
              tb_mat_system_get_perm(self->material_system, submesh->material);
          VkDescriptorSet material_set =
              tb_mat_system_get_set(self->material_system, submesh->material);

          const uint32_t pipe_idx = get_pipeline_for_input_and_mat(
              self, submesh->vertex_input, mat_perm);
          const uint32_t local_pipe_idx = pipe_idxs[pipe_idx];

          MeshDrawBatch *batch = &batches[local_pipe_idx];
          batch->pipeline = self->pipelines[pipe_idx];
          batch->layout = self->pipe_layout; // Can we avoid putting this here?
          batch->view_count = camera_count;
          MeshDrawView *view = &batch->views[cam_idx];
          view->view_set = view_set;
          view->viewport = (VkViewport){0, height, width, -(float)height, 0, 1};
          view->scissor = (VkRect2D){{0, 0}, {width, height}};
          view->draw_count = visible_set->mesh_count;
          MeshDraw *draw = &view->draws[mesh_idx];
          draw->geom_buffer = geom_buffer;
          draw->obj_set = obj_set;
          draw->submesh_draw_count = submesh_draw_idx + 1;
          SubMeshDraw *sub_draw =
              &view->draws[mesh_idx].submesh_draws[submesh_draw_idx];
          *sub_draw = (SubMeshDraw){
              .mat_set = material_set,
              .index_type = submesh->index_type,
              .index_count = submesh->index_count,
              .index_offset = submesh->index_offset,
          };

          submesh_draw_idx++;

          const uint64_t base_vert_offset = submesh->vertex_offset;
          const uint32_t vertex_count = submesh->vertex_count;

          static const uint64_t pos_stride = sizeof(uint16_t) * 4;
          static const uint64_t attr_stride = sizeof(uint16_t) * 2;

          switch (submesh->vertex_input) {
          case VI_P3N3:
            sub_draw->vertex_binding_count = 2;
            sub_draw->vertex_binding_offsets[0] = base_vert_offset;
            sub_draw->vertex_binding_offsets[1] =
                base_vert_offset + (vertex_count * pos_stride);
            break;
          case VI_P3N3U2:
            sub_draw->vertex_binding_count = 3;
            sub_draw->vertex_binding_offsets[0] = base_vert_offset;
            sub_draw->vertex_binding_offsets[1] =
                base_vert_offset + (vertex_count * pos_stride);
            sub_draw->vertex_binding_offsets[2] =
                base_vert_offset + (vertex_count * (pos_stride + attr_stride));
            break;
          case VI_P3N3T4U2:
            sub_draw->vertex_binding_count = 4;
            sub_draw->vertex_binding_offsets[0] = base_vert_offset;
            sub_draw->vertex_binding_offsets[1] =
                base_vert_offset + (vertex_count * pos_stride);
            sub_draw->vertex_binding_offsets[2] =
                base_vert_offset + (vertex_count * (pos_stride + attr_stride));
            sub_draw->vertex_binding_offsets[3] =
                base_vert_offset +
                (vertex_count * (pos_stride + (attr_stride * 2)));
            break;
          default:
            TB_CHECK(false, "Unexepcted vertex input");
            break;
          }
        }
      }
    }
  }

  // Submit batches
  tb_render_pipeline_issue_draw_batch(
      self->render_pipe_system, self->opaque_draw_ctx, batch_count, batches);

  // Similar process for shadow batch
  for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
       ++cascade_idx) {
    const VisibleSet *lit_set = &lit_sets[cascade_idx];

    const View *view = tb_get_view(self->view_system, lit_set->view);

    for (uint32_t mesh_idx = 0; mesh_idx < lit_set->mesh_count; ++mesh_idx) {
      const MeshComponent *mesh_comp = lit_set->meshes[mesh_idx];

      VkBuffer geom_buffer =
          tb_mesh_system_get_gpu_mesh(self, mesh_comp->mesh_id);
      const CommonObjectData *mesh_data = tb_render_object_system_get_data(
          self->render_object_system, mesh_comp->object_id);

      uint32_t submesh_draw_idx = 0;

      for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
           ++sub_idx) {
        const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];

        shadow_batches[cascade_idx].pipeline = self->shadow_pipeline;
        shadow_batches[cascade_idx].layout = self->shadow_pipe_layout;
        shadow_batches[cascade_idx].view_count = dir_light_count;
        ShadowDrawView *draw_view = &shadow_batches[cascade_idx].views[0];
        draw_view->viewport =
            (VkViewport){0, 0, TB_SHADOW_MAP_DIM, TB_SHADOW_MAP_DIM, 0, 1};
        draw_view->scissor =
            (VkRect2D){{0, 0}, {TB_SHADOW_MAP_DIM, TB_SHADOW_MAP_DIM}};
        draw_view->draw_count = lit_set->mesh_count;
        draw_view->consts = (ShadowViewConstants){view->view_data.vp};
        ShadowDraw *draw = &draw_view->draws[mesh_idx];
        draw->consts.m = mesh_data->m;
        draw->geom_buffer = geom_buffer;
        draw->submesh_draw_count = submesh_draw_idx + 1;
        ShadowSubDraw *sub_draw =
            &draw_view->draws[mesh_idx].submesh_draws[submesh_draw_idx];
        *sub_draw = (ShadowSubDraw){
            .index_type = submesh->index_type,
            .index_count = submesh->index_count,
            .index_offset = submesh->index_offset,
            .vertex_binding_offset = submesh->vertex_offset,
        };

        submesh_draw_idx++;
      }
    }
  }

  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    tb_render_pipeline_issue_draw_batch(self->render_pipe_system,
                                        self->shadow_draw_ctxs[i], 1,
                                        &shadow_batches[i]);
  }

  // Output potential transform updates
  {
    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .id = TransformComponentId,
        .count = mesh_count,
        .components = (uint8_t *)out_trans,
        .entities = out_entity_ids,
    };
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(mesh, MeshSystem, MeshSystemDescriptor)

void tb_mesh_system_descriptor(SystemDescriptor *desc,
                               const MeshSystemDescriptor *mesh_desc) {
  desc->name = "Mesh";
  desc->size = sizeof(MeshSystem);
  desc->id = MeshSystemId;
  desc->desc = (InternalDescriptor)mesh_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 3;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {CameraComponentId, TransformComponentId},
  };
  desc->deps[1] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {DirectionalLightComponentId},
  };
  desc->deps[2] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {MeshComponentId, TransformComponentId},
  };
  desc->system_dep_count = 5;
  desc->system_deps[0] = RenderSystemId;
  desc->system_deps[1] = MaterialSystemId;
  desc->system_deps[2] = ViewSystemId;
  desc->system_deps[3] = RenderObjectSystemId;
  desc->system_deps[4] = RenderPipelineSystemId;
  desc->create = tb_create_mesh_system;
  desc->destroy = tb_destroy_mesh_system;
  desc->tick = tb_tick_mesh_system;
}

uint32_t find_mesh_by_id(MeshSystem *self, TbMeshId id) {
  for (uint32_t i = 0; i < self->mesh_count; ++i) {
    if (self->mesh_ids[i] == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

// Based on an example from this cgltf commit message:
// https://github.com/jkuhlmann/cgltf/commit/bd8bd2c9cc08ff9b75a9aa9f99091f7144665c60
static cgltf_result decompress_buffer_view(Allocator alloc,
                                           cgltf_buffer_view *view) {
  if (!view->has_meshopt_compression || view->data != NULL) {
    return cgltf_result_success;
  }

  const cgltf_meshopt_compression *mc = &view->meshopt_compression;
  uint8_t *data = (uint8_t *)mc->buffer->data;
  TB_CHECK_RETURN(data, "Invalid data", cgltf_result_invalid_gltf);

  data += mc->offset;

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

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_node *node) {
  // Hash the mesh's path and gltf name to get the id
  const cgltf_mesh *mesh = node->mesh;
  TB_CHECK_RETURN(mesh, "Given node has no mesh", InvalidMeshId);

  TbMeshId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)mesh->name, SDL_strlen(mesh->name));

  uint32_t index = find_mesh_by_id(self, id);

  // Mesh was not found, load it now
  if (index == SDL_MAX_UINT32) {
    const uint32_t new_count = self->mesh_count + 1;
    if (new_count > self->mesh_max) {
      // Re-allocate space for meshes
      const uint32_t new_max = new_count * 2;

      Allocator alloc = self->std_alloc;

      self->mesh_ids =
          tb_realloc_nm_tp(alloc, self->mesh_ids, new_max, TbMeshId);
      self->mesh_host_buffers = tb_realloc_nm_tp(alloc, self->mesh_host_buffers,
                                                 new_max, TbHostBuffer);
      self->mesh_gpu_buffers =
          tb_realloc_nm_tp(alloc, self->mesh_gpu_buffers, new_max, TbBuffer);
      self->mesh_ref_counts =
          tb_realloc_nm_tp(alloc, self->mesh_ref_counts, new_max, uint32_t);

      self->mesh_max = new_max;
    }

    index = self->mesh_count;

    // Get the mesh node's transform so that we can dequanitze vertices
    // in order to do meshlet generation
    float4x4 dequant = {.row0 = {0}};
    {
      Transform transform = tb_transform_from_node(node);
      transform_to_matrix(&dequant, &transform);
    }

    // TODO: Should be GPU vendor specific
    static const size_t max_vertices = 64;
    static const size_t max_triangles = 124;
    // Not doing cluster cone culling yet
    static const float cone_weight = 0.0f;

    // Determine how big this mesh is
    uint64_t geom_size = 0;
    uint64_t vertex_offset = 0;
    uint64_t meshlet_offset = 0;
    {
      uint64_t index_size = 0;
      uint64_t vertex_size = 0;
      uint64_t meshlets_size = 0;

      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];
        cgltf_accessor *indices = prim->indices;

        index_size += (indices->count * indices->stride);

        for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
             ++attr_idx) {
          // Only care about certain attributes at the moment
          cgltf_attribute_type type = prim->attributes[attr_idx].type;
          int32_t index = prim->attributes[attr_idx].index;
          if ((type == cgltf_attribute_type_position ||
               type == cgltf_attribute_type_normal ||
               type == cgltf_attribute_type_tangent ||
               type == cgltf_attribute_type_texcoord) &&
              index == 0) {
            cgltf_accessor *attr = prim->attributes[attr_idx].data;
            vertex_size += attr->count * attr->stride;
          }
        }

        // Calculate size based off max possible meshlets necessary for this
        // primitive
        meshlets_size += (meshopt_buildMeshletsBound(
                              indices->count, max_vertices, max_triangles) *
                          sizeof(struct meshopt_Meshlet));
      }

      // Calculate the necessary padding between the index and vertex contents
      // of the buffer.
      // Otherwise we'll get a validation error.
      // The vertex content needs to start that the correct attribAddress
      // which must be a multiple of the size of the first attribute
      uint64_t idx_padding = index_size % (sizeof(uint16_t) * 4);
      vertex_offset = index_size + idx_padding;

      // Also need padding between vertices and meshlets
      uint64_t vtx_padding = vertex_size % sizeof(struct meshopt_Meshlet);
      meshlet_offset = vertex_offset + vertex_size + vtx_padding;

      geom_size = meshlet_offset + meshlets_size;
    }

    VkResult err = VK_SUCCESS;

    // Allocate space on the host that we can read the mesh into
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = geom_size,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      };

      char name[100] = {0};
      SDL_snprintf(name, 100, "%s Host Geom Buffer", mesh->name);

      err = tb_rnd_sys_alloc_host_buffer(self->render_system, &create_info,
                                         name, &self->mesh_host_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to create host mesh buffer", false);
    }

    // Create space on the gpu for the mesh
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = geom_size,
          .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      };

      char name[100] = {0};
      SDL_snprintf(name, 100, "%s GPU Geom Buffer", mesh->name);

      err = tb_rnd_sys_alloc_gpu_buffer(self->render_system, &create_info, name,
                                        &self->mesh_gpu_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to create gpu mesh buffer", false);
    }

    // Read the cgltf mesh into the driver owned memory
    {
      TbHostBuffer *host_buf = &self->mesh_host_buffers[index];
      uint64_t idx_offset = 0;
      uint64_t vtx_offset = vertex_offset;
      uint64_t ml_offset = meshlet_offset;
      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];

        {
          cgltf_accessor *indices = prim->indices;
          cgltf_buffer_view *view = indices->buffer_view;
          cgltf_size index_size = indices->count * indices->stride;

          // Decode the buffer
          cgltf_result res = decompress_buffer_view(self->tmp_alloc, view);
          TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

          void *src = ((uint8_t *)view->data) + indices->offset;
          void *dst = ((uint8_t *)(host_buf->ptr)) + idx_offset;
          SDL_memcpy(dst, src, index_size);
          idx_offset += index_size;
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

          size_t attr_offset = accessor->offset;
          size_t attr_size = accessor->stride * accessor->count;

          // Decode the buffer
          cgltf_result res = decompress_buffer_view(self->tmp_alloc, view);
          TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

          void *src = ((uint8_t *)view->data) + attr_offset;
          void *dst = ((uint8_t *)(host_buf->ptr)) + vtx_offset;
          SDL_memcpy(dst, src, attr_size);
          vtx_offset += attr_size;
        }

        // Build meshlets for primitive
        (void)ml_offset;
        (void)cone_weight;
        /* Revisit when HLSL support is more mature
        {
          cgltf_accessor *indices = prim->indices;
          cgltf_buffer_view *view = indices->buffer_view;
          uint8_t *index_data = ((uint8_t *)view->data) + indices->offset;

          if (indices->stride != 4) {
            // Must have 32-bit indices for meshopt, assume existing ones are
            // 16-bit
            uint16_t *old_index_data = (uint16_t *)index_data;
            index_data = (uint8_t *)tb_alloc_nm_tp(self->tmp_alloc,
                                                   indices->count, uint32_t);
            for (cgltf_size i = 0; i < indices->count; ++i) {
              ((uint32_t *)index_data)[i] = (uint32_t)old_index_data[i];
            }
          }

          // Need vertex positions
          float *vertex_positions = NULL;
          size_t vert_count = 0;
          size_t vert_stride = 0;
          {
            // First attribute should be positions
            cgltf_accessor *positions = prim->attributes[0].data;
            TB_CHECK(positions, "Failed to retrieve positions");

            vert_count = positions->count;
            vert_stride = sizeof(float) * 3;

            // Must get vertex positions as floats which means we must
            // dequantize the vertex positions
            vertex_positions =
                tb_alloc(self->tmp_alloc, vert_count * vert_stride);

            uint8_t *quantized_positions =
                ((uint8_t *)positions->buffer_view->data) + positions->offset;
            for (size_t i = 0; i < vert_count; ++i) {
              float *pos = &vertex_positions[i * 3];
              uint16_t *pos_quant =
                  (uint16_t *)&quantized_positions[i * positions->stride];

              float4 to_transform = {pos_quant[0], pos_quant[1], pos_quant[2],
                                     1.0f};
              to_transform = mulf44(dequant, to_transform);
              pos[0] = to_transform[0];
              pos[1] = to_transform[1];
              pos[2] = to_transform[2];
            }
          }

          const size_t max_meshlets = meshopt_buildMeshletsBound(
              indices->count, max_vertices, max_triangles);

          struct meshopt_Meshlet *meshlets =
              (struct meshopt_Meshlet *)(((uint8_t *)(host_buf->ptr)) +
                                         ml_offset);
          uint32_t *meshlet_verts = tb_alloc_nm_tp(
              self->tmp_alloc, max_meshlets * max_vertices, uint32_t);
          uint8_t *meshlet_triangles =
              tb_alloc(self->tmp_alloc, max_meshlets * max_triangles * 3);
          const size_t meshlet_count =
              meshopt_buildMeshlets(meshlets, meshlet_verts, meshlet_triangles,
                                    (uint32_t *)index_data, indices->count,
                                    vertex_positions, vert_count, vert_stride,
                                    max_vertices, max_triangles, cone_weight);

          ml_offset += (meshlet_count * sizeof(struct meshopt_Meshlet));
        }
        */
      }
    }

    // Instruct the render system to upload this
    {
      BufferCopy copy = {
          .src = self->mesh_host_buffers[index].buffer,
          .dst = self->mesh_gpu_buffers[index].buffer,
          .region = {.size = geom_size},
      };
      tb_rnd_upload_buffers(self->render_system, &copy, 1);
    }

    self->mesh_ids[index] = id;
    self->mesh_ref_counts[index] =
        0; // Must initialize this or it could be garbage
    self->mesh_count++;
  }

  self->mesh_ref_counts[index]++;

  return id;
}

bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh", false);

  self->mesh_ref_counts[index]++;

  return true;
}

VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh",
                  VK_NULL_HANDLE);

  VkBuffer buffer = self->mesh_gpu_buffers[index].buffer;
  TB_CHECK_RETURN(buffer, "Failed to retrieve buffer", VK_NULL_HANDLE);

  return buffer;
}

void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);

  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find mesh");
    return;
  }

  if (self->mesh_ref_counts[index] == 0) {
    TB_CHECK(false, "Tried to release reference to mesh with 0 ref count");
    return;
  }

  self->mesh_ref_counts[index]--;

  if (self->mesh_ref_counts[index] == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->render_system->vma_alloc;

    TbHostBuffer *host_buf = &self->mesh_host_buffers[index];
    TbBuffer *gpu_buf = &self->mesh_gpu_buffers[index];

    vmaUnmapMemory(vma_alloc, host_buf->alloc);

    vmaDestroyBuffer(vma_alloc, host_buf->buffer, host_buf->alloc);
    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);

    *host_buf = (TbHostBuffer){0};
    *gpu_buf = (TbBuffer){0};
  }
}
