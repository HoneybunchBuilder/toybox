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
#include "shadow.hlsli"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "vkdbg.h"
#include "world.h"

#include <flecs.h>
#include <meshoptimizer.h>

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
#include "opaque_prepass_frag.h"
#include "opaque_prepass_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

// We know how many input permutations we have
static const uint32_t max_pipe_count = VI_Count * GLTF_PERM_COUNT;

static const uint64_t pos_stride = sizeof(uint16_t) * 4;
static const uint64_t attr_stride = sizeof(uint16_t) * 2;

typedef struct PrimitiveDraw {
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint32_t vertex_binding_count;
  uint64_t vertex_binding_offsets[TB_VERTEX_BINDING_MAX];
  uint32_t instance_count;
} PrimitiveDraw;

typedef struct PrimitiveBatch {
  uint64_t perm;
  VkDescriptorSet view_set;
  VkDescriptorSet inst_set;
  VkDescriptorSet mat_set;
  VkBuffer geom_buffer;
} PrimitiveBatch;

typedef TB_DYN_ARR_OF(PrimitiveBatch) PrimitiveBatchList;
typedef TB_DYN_ARR_OF(TransformList) PrimitiveTransformLists;

typedef struct SubMeshDraw {
  VkDescriptorSet mat_set;
  MaterialPushConstants consts;
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
  TbMaterialPerm mat_perm;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint64_t vertex_binding_offset;
  uint32_t instance_count;
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
  uint32_t view_count;
  ShadowDrawView *views;
} ShadowDrawBatch;

typedef struct VisibleSet {
  TbViewId view;
  uint32_t mesh_count;
  MeshComponent const **meshes;
} VisibleSet;

typedef struct VisibleSet2 {
  TbViewId view;
  TB_DYN_ARR_OF(MeshComponent *) meshes;
} VisibleSet2;

typedef struct TbMesh {
  TbMeshId id;
  uint32_t ref_count;
  TbHostBuffer host_buffer;
  TbBuffer gpu_buffer;
} TbMesh;

VkResult create_shadow_pipeline(RenderSystem *render_system,
                                VkFormat depth_format,
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
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
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
              .cullMode = VK_CULL_MODE_NONE,
              .depthBiasEnable = VK_TRUE,
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
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 4,
              .pDynamicStates =
                  (VkDynamicState[4]){
                      VK_DYNAMIC_STATE_VIEWPORT,
                      VK_DYNAMIC_STATE_SCISSOR,
                      VK_DYNAMIC_STATE_DEPTH_BIAS,
                      VK_DYNAMIC_STATE_CULL_MODE,
                  },
          },
      .layout = pipe_layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Shadow Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create shadow pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_prepass_pipeline(RenderSystem *render_system,
                                 VkFormat depth_format,
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
    err = tb_rnd_create_shader(render_system, &create_info,
                               "Opaque Prepass Vert", &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load opaque prepass vert shader module",
                    err);

    create_info.codeSize = sizeof(opaque_prepass_frag);
    create_info.pCode = (const uint32_t *)opaque_prepass_frag;
    err = tb_rnd_create_shader(render_system, &create_info,
                               "Opaque Prepass Frag", &frag_mod);
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
              .vertexBindingDescriptionCount = 2,
              .pVertexBindingDescriptions =
                  (VkVertexInputBindingDescription[2]){
                      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
                      {1, sizeof(uint16_t) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
                  },
              .vertexAttributeDescriptionCount = 2,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[2]){
                      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
                      {1, 1, VK_FORMAT_R8G8B8A8_SNORM, 0},
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
              .dynamicStateCount = 3,
              .pDynamicStates =
                  (VkDynamicState[3]){
                      VK_DYNAMIC_STATE_VIEWPORT,
                      VK_DYNAMIC_STATE_SCISSOR,
                      VK_DYNAMIC_STATE_CULL_MODE,
                  },
          },
      .layout = pipe_layout,
  };
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Opaque Prepass Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create opaque prepass pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VkResult create_mesh_pipelines(RenderSystem *render_system, Allocator std_alloc,
                               VkFormat color_format, VkFormat depth_format,
                               VkPipelineLayout pipe_layout,
                               uint32_t *pipe_count, VkPipeline **opaque_pipes,
                               VkPipeline **transparent_pipes) {
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
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .colorAttachmentCount = 1,
              .pColorAttachmentFormats = (VkFormat[1]){color_format},
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
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
              .depthWriteEnable = VK_FALSE,
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
              .dynamicStateCount = 3,
              .pDynamicStates =
                  (VkDynamicState[3]){
                      VK_DYNAMIC_STATE_VIEWPORT,
                      VK_DYNAMIC_STATE_SCISSOR,
                      VK_DYNAMIC_STATE_CULL_MODE,
                  },
          },
      .layout = pipe_layout,
  };

  VkGraphicsPipelineCreateInfo opaque_bases[VI_Count] = {0};
  opaque_bases[0] = create_info_base;
  opaque_bases[0].pStages = stages_P3N3;
  opaque_bases[0].pVertexInputState = &vert_input_state_P3N3;
  opaque_bases[1] = create_info_base;
  opaque_bases[1].pStages = stages_P3N3U2;
  opaque_bases[1].pVertexInputState = &vert_input_state_P3N3U2;
  opaque_bases[2] = create_info_base;
  opaque_bases[2].pStages = stages_P3N3T4U2;
  opaque_bases[2].pVertexInputState = &vert_input_state_P3N3T4U2;

  VkGraphicsPipelineCreateInfo trans_base = create_info_base;
  trans_base.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
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
  VkGraphicsPipelineCreateInfo trans_bases[VI_Count] = {0};
  trans_bases[0] = trans_base;
  trans_bases[0].pStages = stages_P3N3;
  trans_bases[0].pVertexInputState = &vert_input_state_P3N3;
  trans_bases[1] = trans_base;
  trans_bases[1].pStages = stages_P3N3U2;
  trans_bases[1].pVertexInputState = &vert_input_state_P3N3U2;
  trans_bases[2] = trans_base;
  trans_bases[2].pStages = stages_P3N3T4U2;
  trans_bases[2].pVertexInputState = &vert_input_state_P3N3T4U2;

  // Create pipelines
  {
    VkPipeline *op_pipes = tb_alloc_nm_tp(std_alloc, VI_Count, VkPipeline);
    err =
        tb_rnd_create_graphics_pipelines(render_system, VI_Count, opaque_bases,
                                         "Opaque Mesh Pipeline", op_pipes);
    TB_VK_CHECK_RET(err, "Failed to create opaque pipelines", err);

    VkPipeline *trans_pipes = tb_alloc_nm_tp(std_alloc, VI_Count, VkPipeline);
    err = tb_rnd_create_graphics_pipelines(render_system, VI_Count, trans_bases,
                                           "Transparent Mesh Pipeline",
                                           trans_pipes);
    TB_VK_CHECK_RET(err, "Failed to create trans pipelines", err);

    *opaque_pipes = op_pipes;
    *transparent_pipes = trans_pipes;
    *pipe_count = VI_Count;
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
                        uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Shadow Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Shadows", 3, true);

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const ShadowDrawBatch *shadow_batch =
        (const ShadowDrawBatch *)batch->user_batch;
    if (shadow_batch->view_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    // TracyCVkNamedZone(gpu_ctx, batch_scope, buffer, "Batch", 4, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    for (uint32_t view_idx = 0; view_idx < shadow_batch->view_count;
         ++view_idx) {
      const ShadowDrawView *view = &shadow_batch->views[view_idx];
      if (view->draw_count == 0) {
        continue;
      }
      TracyCZoneNC(view_ctx, "View", TracyCategoryColorRendering, true);
      // TracyCVkNamedZone(gpu_ctx, view_scope, buffer, "View", 5, true);
      cmd_begin_label(buffer, "View", (float4){0.0f, 0.0f, 0.6f, 1.0f});
      vkCmdSetViewport(buffer, 0, 1, &view->viewport);
      vkCmdSetScissor(buffer, 0, 1, &view->scissor);

      vkCmdSetDepthBias(buffer, 1.25f, 0.0f, 1.75f);

      for (uint32_t draw_idx = 0; draw_idx < view->draw_count; ++draw_idx) {
        const ShadowDraw *draw = &view->draws[draw_idx];
        if (draw->submesh_draw_count == 0) {
          continue;
        }
        TracyCZoneNC(draw_ctx, "Draw", TracyCategoryColorRendering, true);
        // TracyCVkNamedZone(gpu_ctx, mesh_scope, buffer, "Mesh", 6, true);
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
            // TracyCVkNamedZone(gpu_ctx, submesh_scope, buffer, "Submesh", 7,
            //                  true);
            VkCullModeFlags cull_flags = VK_CULL_MODE_BACK_BIT;
            if (submesh->mat_perm & GLTF_PERM_DOUBLE_SIDED) {
              cull_flags = VK_CULL_MODE_NONE;
            }
            vkCmdSetCullMode(buffer, cull_flags);
            vkCmdBindIndexBuffer(buffer, geom_buffer, submesh->index_offset,
                                 submesh->index_type);
            vkCmdBindVertexBuffers(buffer, 0, 1, &geom_buffer,
                                   &submesh->vertex_binding_offset);

            vkCmdDrawIndexed(buffer, submesh->index_count, 1, 0, 0, 0);
            // TracyCVkZoneEnd(submesh_scope);
          }
          TracyCZoneEnd(submesh_ctx);
        }
        cmd_end_label(buffer);
        // TracyCVkZoneEnd(mesh_scope);
        TracyCZoneEnd(draw_ctx);
      }
      cmd_end_label(buffer);
      // TracyCVkZoneEnd(view_scope);
      TracyCZoneEnd(view_ctx);
    }

    cmd_end_label(buffer);
    // TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }

  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void prepass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                    uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Prepass Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Prepass Meshes", 3,
                    true);
  cmd_begin_label(buffer, "Prepass Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const MeshDrawBatch *mesh_batch = (const MeshDrawBatch *)batch->user_batch;
    if (mesh_batch->view_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    // TracyCVkNamedZone(gpu_ctx, batch_scope, buffer,
    // "Batch", 4, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    for (uint32_t view_idx = 0; view_idx < mesh_batch->view_count; ++view_idx) {
      const MeshDrawView *view = &mesh_batch->views[view_idx];
      if (view->draw_count == 0) {
        continue;
      }

      TracyCZoneNC(view_ctx, "View", TracyCategoryColorRendering, true);
      // TracyCVkNamedZone(gpu_ctx, view_scope, buffer,
      // "View", 5, true);
      cmd_begin_label(buffer, "View", (float4){0.0f, 0.0f, 0.6f, 1.0f});

      vkCmdSetViewport(buffer, 0, 1, &view->viewport);
      vkCmdSetScissor(buffer, 0, 1, &view->scissor);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              1, 1, &view->view_set, 0, NULL);
      for (uint32_t draw_idx = 0; draw_idx < view->draw_count; ++draw_idx) {
        const MeshDraw *draw = &view->draws[draw_idx];
        if (draw->submesh_draw_count == 0) {
          continue;
        }

        TracyCZoneNC(draw_ctx, "Draw", TracyCategoryColorRendering, true);
        // TracyCVkNamedZone(gpu_ctx, mesh_scope, buffer,
        // "Mesh", 6, true);
        cmd_begin_label(buffer, "Mesh", (float4){0.0f, 0.0f, 0.4f, 1.0f});

        vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                0, 1, &draw->obj_set, 0, NULL);
        VkBuffer geom_buffer = draw->geom_buffer;

        for (uint32_t sub_idx = 0; sub_idx < draw->submesh_draw_count;
             ++sub_idx) {
          TracyCZoneNC(submesh_ctx, "Submesh", TracyCategoryColorRendering,
                       true);
          const SubMeshDraw *submesh = &draw->submesh_draws[sub_idx];
          if (submesh->index_count > 0) {
            // TracyCVkNamedZone(gpu_ctx, submesh_scope,
            // buffer, "Submesh", 7,
            // true);
            VkCullModeFlags cull_flags = VK_CULL_MODE_BACK_BIT;
            if (submesh->consts.perm & GLTF_PERM_DOUBLE_SIDED) {
              cull_flags = VK_CULL_MODE_NONE;
            }
            vkCmdSetCullMode(buffer, cull_flags);
            // Don't need to bind material data
            vkCmdBindIndexBuffer(buffer, geom_buffer, submesh->index_offset,
                                 submesh->index_type);
            for (uint32_t vb_idx = 0; vb_idx < submesh->vertex_binding_count;
                 ++vb_idx) {
              vkCmdBindVertexBuffers(buffer, vb_idx, 1, &geom_buffer,
                                     &submesh->vertex_binding_offsets[vb_idx]);
            }

            vkCmdDrawIndexed(buffer, submesh->index_count, 1, 0, 0, 0);
            // TracyCVkZoneEnd(submesh_scope);
          }
          TracyCZoneEnd(submesh_ctx);
        }

        cmd_end_label(buffer);
        // TracyCVkZoneEnd(mesh_scope);
        TracyCZoneEnd(draw_ctx);
      }

      cmd_end_label(buffer);
      // TracyCVkZoneEnd(view_scope);
      TracyCZoneEnd(view_ctx);
    }

    cmd_end_label(buffer);
    // TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void mesh_record_common(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const DrawBatch *batches) {
  (void)gpu_ctx;
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const MeshDrawBatch *mesh_batch = (const MeshDrawBatch *)batch->user_batch;
    if (mesh_batch->view_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    // TracyCVkNamedZone(gpu_ctx, batch_scope, buffer,
    // "Batch", 4, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    for (uint32_t view_idx = 0; view_idx < mesh_batch->view_count; ++view_idx) {
      const MeshDrawView *view = &mesh_batch->views[view_idx];
      if (view->draw_count == 0) {
        continue;
      }

      TracyCZoneNC(view_ctx, "View", TracyCategoryColorRendering, true);
      // TracyCVkNamedZone(gpu_ctx, view_scope, buffer,
      // "View", 5, true);
      cmd_begin_label(buffer, "View", (float4){0.0f, 0.0f, 0.6f, 1.0f});

      vkCmdSetViewport(buffer, 0, 1, &view->viewport);
      vkCmdSetScissor(buffer, 0, 1, &view->scissor);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              2, 1, &view->view_set, 0, NULL);
      for (uint32_t draw_idx = 0; draw_idx < view->draw_count; ++draw_idx) {
        const MeshDraw *draw = &view->draws[draw_idx];
        if (draw->submesh_draw_count == 0) {
          continue;
        }

        TracyCZoneNC(draw_ctx, "Draw", TracyCategoryColorRendering, true);
        // TracyCVkNamedZone(gpu_ctx, mesh_scope, buffer,
        // "Mesh", 6, true);
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
            // TracyCVkNamedZone(gpu_ctx, submesh_scope,
            // buffer, "Submesh", 7,
            //                 true);
            vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    layout, 0, 1, &submesh->mat_set, 0, NULL);
            vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(MaterialPushConstants), &submesh->consts);
            VkCullModeFlags cull_flags = VK_CULL_MODE_BACK_BIT;
            if (submesh->consts.perm & GLTF_PERM_DOUBLE_SIDED) {
              cull_flags = VK_CULL_MODE_NONE;
            }
            vkCmdSetCullMode(buffer, cull_flags);
            vkCmdBindIndexBuffer(buffer, geom_buffer, submesh->index_offset,
                                 submesh->index_type);
            for (uint32_t vb_idx = 0; vb_idx < submesh->vertex_binding_count;
                 ++vb_idx) {
              vkCmdBindVertexBuffers(buffer, vb_idx, 1, &geom_buffer,
                                     &submesh->vertex_binding_offsets[vb_idx]);
            }

            vkCmdDrawIndexed(buffer, submesh->index_count, 1, 0, 0, 0);
            // TracyCVkZoneEnd(submesh_scope);
          }
          TracyCZoneEnd(submesh_ctx);
        }

        cmd_end_label(buffer);
        // TracyCVkZoneEnd(mesh_scope);
        TracyCZoneEnd(draw_ctx);
      }

      cmd_end_label(buffer);
      // TracyCVkZoneEnd(view_scope);
      TracyCZoneEnd(view_ctx);
    }

    cmd_end_label(buffer);
    // TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }
}

void opaque_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Meshes", 3, true);
  cmd_begin_label(buffer, "Opaque Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void transparent_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                             uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Transparent Record", TracyCategoryColorRendering,
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

void prepass_record2(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                     uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Prepass Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Prepass Meshes", 3,
                    true);
  cmd_begin_label(buffer, "Prepass Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const PrimitiveBatch *prim_batch =
        (const PrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkBuffer geom_buffer = prim_batch->geom_buffer;

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                            1, &prim_batch->view_set, 0, NULL);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            1, &prim_batch->inst_set, 0, NULL);
    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      const PrimitiveDraw *draw =
          &((const PrimitiveDraw *)batch->draws)[draw_idx];
      if (draw->instance_count == 0) {
        continue;
      }

      TracyCZoneNC(draw_ctx, "Batch", TracyCategoryColorRendering, true);
      cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.4f, 1.0f});

      if (draw->index_count > 0) {
        VkCullModeFlags cull_flags = VK_CULL_MODE_BACK_BIT;
        if (prim_batch->perm & GLTF_PERM_DOUBLE_SIDED) {
          cull_flags = VK_CULL_MODE_NONE;
        }
        vkCmdSetCullMode(buffer, cull_flags);
        // Don't need to bind material data
        vkCmdBindIndexBuffer(buffer, geom_buffer, draw->index_offset,
                             draw->index_type);
        // We only need the first two vertex bindings for positions and normals
        // Those are expected to always be the first two bindings
        for (uint32_t vb_idx = 0; vb_idx < 2; ++vb_idx) {
          vkCmdBindVertexBuffers(buffer, vb_idx, 1, &geom_buffer,
                                 &draw->vertex_binding_offsets[vb_idx]);
        }
        vkCmdDrawIndexed(buffer, draw->index_count, draw->instance_count, 0, 0,
                         0);
      }

      cmd_end_label(buffer);
      TracyCZoneEnd(draw_ctx);
    }

    cmd_end_label(buffer);
    TracyCZoneEnd(batch_ctx);
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void mesh_record_common2(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                         uint32_t batch_count, const DrawBatch *batches) {
  (void)gpu_ctx;
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const PrimitiveBatch *prim_batch =
        (const PrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Batch", TracyCategoryColorRendering, true);
    cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    VkBuffer geom_buffer = prim_batch->geom_buffer;

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2,
                            1, &prim_batch->view_set, 0, NULL);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                            1, &prim_batch->inst_set, 0, NULL);
    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      const PrimitiveDraw *draw =
          &((const PrimitiveDraw *)batch->draws)[draw_idx];
      if (draw->instance_count == 0) {
        continue;
      }

      TracyCZoneNC(draw_ctx, "Batch", TracyCategoryColorRendering, true);
      cmd_begin_label(buffer, "Batch", (float4){0.0f, 0.0f, 0.4f, 1.0f});

      if (draw->index_count > 0) {
        VkCullModeFlags cull_flags = VK_CULL_MODE_BACK_BIT;
        if (prim_batch->perm & GLTF_PERM_DOUBLE_SIDED) {
          cull_flags = VK_CULL_MODE_NONE;
        }
        vkCmdSetCullMode(buffer, cull_flags);

        // Bind material data
        vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                0, 1, &prim_batch->mat_set, 0, NULL);
        vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MaterialPushConstants),
                           &(MaterialPushConstants){prim_batch->perm});

        vkCmdBindIndexBuffer(buffer, geom_buffer, draw->index_offset,
                             draw->index_type);
        for (uint32_t vb_idx = 0; vb_idx < draw->vertex_binding_count;
             ++vb_idx) {
          vkCmdBindVertexBuffers(buffer, vb_idx, 1, &geom_buffer,
                                 &draw->vertex_binding_offsets[vb_idx]);
        }

        vkCmdDrawIndexed(buffer, draw->index_count, draw->instance_count, 0, 0,
                         0);
      }

      cmd_end_label(buffer);
      TracyCZoneEnd(draw_ctx);
    }

    cmd_end_label(buffer);
    TracyCZoneEnd(batch_ctx);
  }
}

void opaque_pass_record2(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                         uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Opaque Meshes", 3, true);
  cmd_begin_label(buffer, "Opaque Meshes", (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common2(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void transparent_pass_record2(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                              uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Mesh Transparent Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Transparent Meshes", 3,
                    true);
  cmd_begin_label(buffer, "Transparent Meshes",
                  (float4){0.0f, 0.0f, 1.0f, 1.0f});
  mesh_record_common2(gpu_ctx, buffer, batch_count, batches);
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

MeshSystem create_mesh_system_internal(
    Allocator std_alloc, Allocator tmp_alloc, RenderSystem *render_system,
    MaterialSystem *material_system, ViewSystem *view_system,
    RenderObjectSystem *render_object_system,
    RenderPipelineSystem *render_pipe_system) {
  MeshSystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .render_system = render_system,
      .material_system = material_system,
      .view_system = view_system,
      .render_object_system = render_object_system,
      .render_pipe_system = render_pipe_system,
  };
  TB_DYN_ARR_RESET(sys.meshes, std_alloc, 8);
  TbRenderPassId prepass_id = render_pipe_system->opaque_depth_normal_pass;
  TbRenderPassId opaque_pass_id = render_pipe_system->opaque_color_pass;
  TbRenderPassId transparent_pass_id =
      render_pipe_system->transparent_color_pass;

  // Setup mesh system for rendering
  {
    VkResult err = VK_SUCCESS;

    // Get descriptor set layouts from related systems
    {
      sys.obj_set_layout = render_object_system->set_layout;
      sys.view_set_layout = view_system->set_layout;
    }

    // Create prepass pipeline layout
    {
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 2,
          .pSetLayouts =
              (VkDescriptorSetLayout[2]){
                  sys.obj_set_layout,
                  sys.view_set_layout,
              },
      };
      err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                          "Opaque Depth Normal Prepass Layout",
                                          &sys.prepass_layout);
    }

    // Create prepass pipeline
    {
      VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
      err = create_prepass_pipeline(sys.render_system, depth_format,
                                    sys.prepass_layout, &sys.prepass_pipe);
      TB_VK_CHECK(err, "Failed to create opaque prepass pipeline");
    }

    // Create pipeline layouts
    {
#define LAYOUT_COUNT 3
      VkDescriptorSetLayout layouts[LAYOUT_COUNT] = {
          material_system->set_layout,
          sys.obj_set_layout,
          sys.view_set_layout,
      };

      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = LAYOUT_COUNT,
          .pSetLayouts = layouts,
          .pushConstantRangeCount = 1,
          .pPushConstantRanges =
              (VkPushConstantRange[1]){
                  {
                      .offset = 0,
                      .size = sizeof(MaterialPushConstants),
                      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                  },
              },
      };
#undef LAYOUT_COUNT
      err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                          "GLTF Pipeline Layout",
                                          &sys.pipe_layout);
    }

    // Create opaque and transparent pipelines
    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(
          sys.render_pipe_system,
          sys.render_pipe_system->opaque_depth_normal_pass, &attach_count,
          NULL);
      TB_CHECK(attach_count == 2, "Unexpected");
      PassAttachment depth_info = {0};
      tb_render_pipeline_get_attachments(
          sys.render_pipe_system,
          sys.render_pipe_system->opaque_depth_normal_pass, &attach_count,
          &depth_info);

      VkFormat depth_format = tb_render_target_get_format(
          sys.render_pipe_system->render_target_system, depth_info.attachment);

      VkFormat color_format = VK_FORMAT_UNDEFINED;
      tb_render_pipeline_get_attachments(
          sys.render_pipe_system, sys.render_pipe_system->opaque_color_pass,
          &attach_count, NULL);
      TB_CHECK(attach_count == 2, "Unexpected");
      PassAttachment attach_info[2] = {0};
      tb_render_pipeline_get_attachments(
          sys.render_pipe_system, sys.render_pipe_system->opaque_color_pass,
          &attach_count, attach_info);

      for (uint32_t i = 0; i < attach_count; i++) {
        VkFormat format = tb_render_target_get_format(
            sys.render_pipe_system->render_target_system,
            attach_info[i].attachment);
        if (format != VK_FORMAT_D32_SFLOAT) {
          color_format = format;
          break;
        }
      }
      err = create_mesh_pipelines(sys.render_system, sys.std_alloc,
                                  color_format, depth_format, sys.pipe_layout,
                                  &sys.pipe_count, &sys.opaque_pipelines,
                                  &sys.transparent_pipelines);
      TB_VK_CHECK(err, "Failed to create mesh pipelines");
    }

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
                                          &sys.shadow_pipe_layout);
      TB_VK_CHECK(err, "Failed to create shadow pipeline layout");
    }

    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(sys.render_pipe_system,
                                         sys.render_pipe_system->shadow_pass,
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      PassAttachment depth_info = {0};
      tb_render_pipeline_get_attachments(sys.render_pipe_system,
                                         sys.render_pipe_system->shadow_pass,
                                         &attach_count, &depth_info);

      VkFormat depth_format = tb_render_target_get_format(
          sys.render_pipe_system->render_target_system, depth_info.attachment);
      err =
          create_shadow_pipeline(sys.render_system, depth_format,
                                 sys.shadow_pipe_layout, &sys.shadow_pipeline);
      TB_VK_CHECK(err, "Failed to create shadow pipeline");
    }
  }
  // Register drawing with the pipelines
  sys.prepass_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(MeshDrawBatch),
                              .draw_fn = prepass_record,
                              .pass_id = prepass_id,
                          });
  sys.opaque_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(MeshDrawBatch),
                              .draw_fn = opaque_pass_record,
                              .pass_id = opaque_pass_id,
                          });
  sys.transparent_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(MeshDrawBatch),
                              .draw_fn = transparent_pass_record,
                              .pass_id = transparent_pass_id,
                          });
  sys.shadow_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(MeshDrawBatch),
                              .draw_fn = shadow_pass_record,
                              .pass_id = render_pipe_system->shadow_pass,
                          });
  // Gen 2
  sys.prepass_draw_ctx2 = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(PrimitiveBatch),
                              .draw_fn = prepass_record2,
                              .pass_id = prepass_id,
                          });
  sys.opaque_draw_ctx2 = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(PrimitiveBatch),
                              .draw_fn = opaque_pass_record2,
                              .pass_id = opaque_pass_id,
                          });
  sys.transparent_draw_ctx2 = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(PrimitiveBatch),
                              .draw_fn = transparent_pass_record2,
                              .pass_id = transparent_pass_id,
                          });
  return sys;
}

void destroy_mesh_system(MeshSystem *self) {
  RenderSystem *render_system = self->render_system;

  tb_rnd_destroy_pipeline(render_system, self->shadow_pipeline);
  for (uint32_t i = 0; i < self->pipe_count; ++i) {
    tb_rnd_destroy_pipeline(render_system, self->opaque_pipelines[i]);
    tb_rnd_destroy_pipeline(render_system, self->transparent_pipelines[i]);
  }
  tb_rnd_destroy_pipeline(render_system, self->prepass_pipe);

  tb_rnd_destroy_pipe_layout(render_system, self->shadow_pipe_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->prepass_layout);

  TB_DYN_ARR_FOREACH(self->meshes, i) {
    if (TB_DYN_ARR_AT(self->meshes, i).ref_count != 0) {
      TB_CHECK(false, "Leaking meshes");
    }
  }

  TB_DYN_ARR_DESTROY(self->meshes);

  *self = (MeshSystem){0};
}

uint32_t get_pipeline_for_input(MeshSystem *self, TbVertexInput input) {
  TracyCZone(ctx, true);
  // We know the layout of the distribution of pipelines so we
  // can decode the vertex input and the material permutation
  // from the index
  for (uint32_t pipe_idx = 0; pipe_idx < self->pipe_count; ++pipe_idx) {
    const TbVertexInput vi = pipe_idx;

    if (input == vi) {
      TracyCZoneEnd(ctx);
      return pipe_idx;
    }
  }
  TracyCZoneEnd(ctx);
  TB_CHECK_RETURN(false, "Failed to find pipeline for given mesh permutations",
                  SDL_MAX_UINT32);
}

uint32_t find_mesh_by_id(MeshSystem *self, TbMeshId id) {
  TB_DYN_ARR_FOREACH(self->meshes, i) {
    if (TB_DYN_ARR_AT(self->meshes, i).id == id) {
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
  if (view->data != NULL) {
    // Already decoded
    return cgltf_result_success;
  }

  // Uncompressed buffer? No problem
  if (!view->has_meshopt_compression) {
    uint8_t *data = (uint8_t *)view->buffer->data;
    data += view->offset;

    uint8_t *result = tb_alloc(alloc, view->size);
    SDL_memcpy(result, data, view->size);
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

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_node *node) {
  // Hash the mesh's path and the cgltf_mesh structure to get
  // an id We'd prefer to use a name but gltfpack is currently
  // stripping mesh names
  const cgltf_mesh *mesh = node->mesh;
  TB_CHECK_RETURN(mesh, "Given node has no mesh", InvalidMeshId);

  TbMeshId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)mesh, sizeof(cgltf_mesh));

  uint32_t index = find_mesh_by_id(self, id);

  // Mesh was not found, load it now
  if (index == SDL_MAX_UINT32) {
    index = TB_DYN_ARR_SIZE(self->meshes);
    {
      TbMesh m = {.id = id};
      TB_DYN_ARR_APPEND(self->meshes, m);
    }
    TbMesh *tb_mesh = &TB_DYN_ARR_AT(self->meshes, index);

    // Determine how big this mesh is
    uint64_t geom_size = 0;
    uint64_t vertex_offset = 0;
    {
      uint64_t index_size = 0;
      uint64_t vertex_size = 0;

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
      }

      // Calculate the necessary padding between the index and
      // vertex contents of the buffer. Otherwise we'll get a
      // validation error. The vertex content needs to start
      // that the correct attribAddress which must be a
      // multiple of the size of the first attribute
      uint64_t idx_padding = index_size % (sizeof(uint16_t) * 4);
      vertex_offset = index_size + idx_padding;

      geom_size = vertex_offset + vertex_size;
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
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      };
      err = tb_rnd_sys_create_gpu_buffer(self->render_system, &create_info,
                                         mesh->name, &tb_mesh->gpu_buffer,
                                         &tb_mesh->host_buffer, &ptr);
      TB_VK_CHECK_RET(err, "Failed to create mesh buffer", false);
    }

    // Read the cgltf mesh into the driver owned memory
    {
      uint64_t idx_offset = 0;
      uint64_t vtx_offset = vertex_offset;
      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];

        {
          cgltf_accessor *indices = prim->indices;
          cgltf_buffer_view *view = indices->buffer_view;
          cgltf_size index_size = indices->count * indices->stride;

          // Decode the buffer
          cgltf_result res = decompress_buffer_view(self->std_alloc, view);
          TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

          void *src = ((uint8_t *)view->data) + indices->offset;
          void *dst = ((uint8_t *)(ptr)) + idx_offset;
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
          cgltf_result res = decompress_buffer_view(self->std_alloc, view);
          TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

          void *src = ((uint8_t *)view->data) + attr_offset;
          void *dst = ((uint8_t *)(ptr)) + vtx_offset;
          SDL_memcpy(dst, src, attr_size);
          vtx_offset += attr_size;
        }
      }
    }
  }

  TB_DYN_ARR_AT(self->meshes, index).ref_count++;
  return id;
}

bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh", false);
  TB_DYN_ARR_AT(self->meshes, index).ref_count++;
  return true;
}

VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh",
                  VK_NULL_HANDLE);

  VkBuffer buffer = TB_DYN_ARR_AT(self->meshes, index).gpu_buffer.buffer;
  TB_CHECK_RETURN(buffer, "Failed to retrieve buffer", VK_NULL_HANDLE);

  return buffer;
}

void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);

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
    VmaAllocator vma_alloc = self->render_system->vma_alloc;

    TbHostBuffer *host_buf = &mesh->host_buffer;
    TbBuffer *gpu_buf = &mesh->gpu_buffer;

    vmaUnmapMemory(vma_alloc, host_buf->alloc);

    vmaDestroyBuffer(vma_alloc, host_buf->buffer, host_buf->alloc);
    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);

    *host_buf = (TbHostBuffer){0};
    *gpu_buf = (TbBuffer){0};
  }
}

void mesh_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Mesh System", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, CameraComponent);
  ECS_COMPONENT(ecs, MeshComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, MeshSystem);
  ECS_COMPONENT(ecs, MaterialSystem);
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  ECS_COMPONENT(ecs, RenderObjectSystem);
  ECS_COMPONENT(ecs, ViewSystem);

  MeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, MeshSystem);
  MaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, MaterialSystem);
  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, RenderPipelineSystem);
  RenderObjectSystem *ro_sys = ecs_singleton_get_mut(ecs, RenderObjectSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);

  const uint32_t camera_count = it->count;
  Allocator tmp_alloc = mesh_sys->tmp_alloc;

  CameraComponent *cameras = ecs_field(it, CameraComponent, 1);
  // TransformComponent *camera_transforms = ecs_field(it, TransformComponent,
  // 2);

  TB_DYN_ARR_OF(VisibleSet2) vis_sets = {0};
  TB_DYN_ARR_RESET(vis_sets, tmp_alloc, it->count);

  // Run some queries that we will iterate
  ecs_iter_t mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
  const int32_t mesh_count = ecs_iter_count(&mesh_it);

  ecs_iter_t dir_light_it = ecs_query_iter(ecs, mesh_sys->dir_light_query);

  // Update each mesh's render object data while also
  // collecting world space AABBs for culling later
  AABB *world_space_aabbs = tb_alloc_nm_tp(tmp_alloc, mesh_count, AABB);

  mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
  while (ecs_query_next(&mesh_it)) {
    MeshComponent *meshes = ecs_field(&mesh_it, MeshComponent, 1);
    TransformComponent *transforms = ecs_field(&mesh_it, TransformComponent, 2);

    // Query each mesh against the view to determine if it should be drawn
    // and add it to the visible list
    for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
      MeshComponent *mesh = &meshes[mesh_idx];
      TransformComponent *trans = &transforms[mesh_idx];

      // Convert the transform to a matrix for rendering
      CommonObjectData data = {
          .m = tb_transform_get_world_matrix(ecs, trans),
      };

      // Transform local aabb into world space
      AABB aabb = aabb_init();
      {
        float4 min = f3tof4(mesh->local_aabb.min, 1.0f);
        float4 max = f3tof4(mesh->local_aabb.max, 1.0f);

        min = mulf44(data.m, min);
        max = mulf44(data.m, max);

        aabb_add_point(&aabb, f4tof3(min));
        aabb_add_point(&aabb, f4tof3(max));
      }
      world_space_aabbs[mesh_idx] = aabb;

      tb_render_object_system_set_object_data(ro_sys, mesh->object_id, &data);
    }
  }

  // For each camera
  for (int32_t cam_idx = 0; cam_idx < it->count; ++cam_idx) {
    CameraComponent *camera = &cameras[cam_idx];
    // TransformComponent *cam_trans = &camera_transforms[cam_idx];

    VisibleSet2 vis_set = {.view = camera->view_id};
    TB_DYN_ARR_RESET(vis_set.meshes, tmp_alloc, mesh_count);

    mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
    while (ecs_query_next(&mesh_it)) {
      MeshComponent *meshes = ecs_field(&mesh_it, MeshComponent, 1);
      // TransformComponent *transforms =
      //     ecs_field(&mesh_it, TransformComponent, 2);

      // Query each mesh against the view to determine if it should be drawn
      // and add it to the visible list
      for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
        MeshComponent *mesh = &meshes[mesh_idx];
        // TransformComponent *trans = &transforms[mesh_idx];

        // For now be evil and just add everything
        TB_DYN_ARR_APPEND(vis_set.meshes, mesh);
      }
    }
    TB_DYN_ARR_APPEND(vis_sets, vis_set);
  }

  TB_DYN_ARR_OF(VisibleSet2) lit_sets = {0};
  TB_DYN_ARR_RESET(lit_sets, tmp_alloc, TB_CASCADE_COUNT);
  {
    TracyCZoneN(light_ctx, "Light Visibility", true);

    dir_light_it = ecs_query_iter(ecs, mesh_sys->dir_light_query);
    while (ecs_query_next(&dir_light_it)) {
      const DirectionalLightComponent *light =
          ecs_field(&dir_light_it, DirectionalLightComponent, 1);

      for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
           ++cascade_idx) {

        VisibleSet2 *lit_set = &TB_DYN_ARR_AT(lit_sets, cascade_idx);

        *lit_set = (VisibleSet2){
            .view = light->cascade_views[cascade_idx],
        };
        TB_DYN_ARR_RESET(lit_set->meshes, tmp_alloc, mesh_it.count);
      }

      for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
           ++cascade_idx) {
        VisibleSet2 *lit_set = &TB_DYN_ARR_AT(lit_sets, cascade_idx);

        mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
        while (ecs_query_next(&mesh_it)) {
          MeshComponent *meshes = ecs_field(&mesh_it, MeshComponent, 1);
          // TransformComponent *transforms =
          //     ecs_field(&mesh_it, TransformComponent, 2);

          // Query each mesh against the view to determine if it should be drawn
          // and add it to the visible list
          for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
            MeshComponent *mesh = &meshes[mesh_idx];
            // TransformComponent *trans = &transforms[mesh_idx];

            // For now be evil and just add everything
            TB_DYN_ARR_APPEND(lit_set->meshes, mesh);
          }
        }
      }
    }
    TracyCZoneEnd(light_ctx);
  }

  Allocator rnd_tmp_alloc =
      rnd_sys->render_thread->frame_states[rnd_sys->frame_idx].tmp_alloc.alloc;

  // Figure out # of unique pipelines so we know how many
  // batches we have
  uint32_t pipe_count = 0;
  uint32_t *pipe_idxs = tb_alloc_nm_tp(tmp_alloc, max_pipe_count, uint32_t);
  SDL_memset(pipe_idxs, 0, sizeof(uint32_t) * max_pipe_count);
  {
    TracyCZoneN(cull_ctx, "Batch Culling", true);
    for (int32_t view_idx = 0; view_idx < it->count; ++view_idx) {
      const VisibleSet2 *vis_set = &TB_DYN_ARR_AT(vis_sets, view_idx);
      TB_DYN_ARR_FOREACH(vis_set->meshes, mesh_idx) {
        const MeshComponent *mesh_comp =
            TB_DYN_ARR_AT(vis_set->meshes, mesh_idx);
        for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
             ++sub_idx) {
          const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];
          uint32_t pipe_idx =
              get_pipeline_for_input(mesh_sys, submesh->vertex_input);

          if (pipe_idxs[pipe_idx] == 0) {
            pipe_idxs[pipe_idx] = pipe_count;
            pipe_count++;
            TB_CHECK(pipe_count <= max_pipe_count, "Unexpected # of pipelines");
          }
        }
      }
    }
    TracyCZoneEnd(cull_ctx);
  }

  // Just collect an opaque and transparent batch for every
  // known used pipeline
  const uint32_t batch_count = pipe_count;

  TB_PROF_MESSAGE("Batch Count: %d", batch_count);

  // Allocate and initialize prepass batch per vertex input
  // layout
  DrawBatch *prepass_batch = NULL;
  MeshDrawBatch *prepass_user_batch = NULL;
  {
    TracyCZoneN(batch_ctx, "Allocate Prepass Batch", true);
    prepass_batch = tb_alloc_nm_tp(rnd_tmp_alloc, 1, DrawBatch);
    prepass_user_batch = tb_alloc_nm_tp(rnd_tmp_alloc, 1, MeshDrawBatch);

    // Batch could use each view
    *prepass_user_batch = (MeshDrawBatch){0};
    prepass_user_batch->views =
        tb_alloc_nm_tp(rnd_tmp_alloc, camera_count, MeshDrawView);
    prepass_user_batch->view_count = 0;
    prepass_batch->user_batch = prepass_user_batch;

    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      MeshDrawView *view = &prepass_user_batch->views[cam_idx];
      // Each view already knows how many meshes it should see
      // Each mesh could have TB_SUBMESH_MAX # of submeshes
      *view = (MeshDrawView){0};
      const VisibleSet2 *vis_set = &TB_DYN_ARR_AT(vis_sets, cam_idx);
      const uint32_t mesh_count = TB_DYN_ARR_SIZE(vis_set->meshes);
      view->draws = tb_alloc_nm_tp(rnd_tmp_alloc, mesh_count, MeshDraw);
      view->draw_count = 0;

      SDL_memset(view->draws, 0, sizeof(MeshDraw) * mesh_count);

      prepass_batch->pipeline = mesh_sys->prepass_pipe;
      prepass_batch->layout = mesh_sys->prepass_layout;
    }

    TracyCZoneEnd(batch_ctx);
  }

  // Allocate and initialize each opaque batch
  DrawBatch *opaque_batches = NULL;
  MeshDrawBatch *opaque_user_batches = NULL;
  {
    TracyCZoneN(batch_ctx, "Allocate Opaque Batches", true);
    opaque_batches = tb_alloc_nm_tp(rnd_tmp_alloc, batch_count, DrawBatch);
    opaque_user_batches =
        tb_alloc_nm_tp(rnd_tmp_alloc, batch_count, MeshDrawBatch);
    for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
      // Each batch could use each view
      MeshDrawBatch *batch = &opaque_user_batches[batch_idx];
      *batch = (MeshDrawBatch){0};
      batch->views = tb_alloc_nm_tp(rnd_tmp_alloc, camera_count, MeshDrawView);
      batch->view_count = 0;
      opaque_batches[batch_idx].user_batch = batch;

      for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
        MeshDrawView *view = &batch->views[cam_idx];
        // Each view already knows how many meshes it should
        // see Each mesh could have TB_SUBMESH_MAX # of
        // submeshes
        const VisibleSet2 *vis_set = &TB_DYN_ARR_AT(vis_sets, cam_idx);
        const uint32_t mesh_count = TB_DYN_ARR_SIZE(vis_set->meshes);

        *view = (MeshDrawView){0};
        view->draws = tb_alloc_nm_tp(rnd_tmp_alloc, mesh_count, MeshDraw);
        view->draw_count = 0;

        SDL_memset(view->draws, 0, sizeof(MeshDraw) * mesh_count);
      }
    }
    TracyCZoneEnd(batch_ctx);
  }

  // Allocate and initialize each transparent batch
  DrawBatch *transparent_batches = NULL;
  MeshDrawBatch *transparent_user_batches = NULL;
  {
    TracyCZoneN(batch_ctx, "Allocate Transparent Batches", true);
    transparent_batches = tb_alloc_nm_tp(rnd_tmp_alloc, batch_count, DrawBatch);
    transparent_user_batches =
        tb_alloc_nm_tp(rnd_tmp_alloc, batch_count, MeshDrawBatch);
    for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
      // Each batch could use each view
      MeshDrawBatch *batch = &transparent_user_batches[batch_idx];
      *batch = (MeshDrawBatch){0};
      batch->views = tb_alloc_nm_tp(rnd_tmp_alloc, camera_count, MeshDrawView);
      batch->view_count = 0;
      transparent_batches[batch_idx].user_batch = batch;

      for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
        MeshDrawView *view = &batch->views[cam_idx];
        // Each view already knows how many meshes it should
        // see Each mesh could have TB_SUBMESH_MAX # of
        // submeshes
        const VisibleSet2 *vis_set = &TB_DYN_ARR_AT(vis_sets, cam_idx);
        const uint32_t mesh_count = TB_DYN_ARR_SIZE(vis_set->meshes);

        *view = (MeshDrawView){0};
        view->draws = tb_alloc_nm_tp(rnd_tmp_alloc, mesh_count, MeshDraw);
        view->draw_count = 0;

        SDL_memset(view->draws, 0, sizeof(MeshDraw) * mesh_count);
      }
    }
    TracyCZoneEnd(batch_ctx);
  }

  // Create one batch for each shadow cascade
  DrawBatch shadow_batches[TB_CASCADE_COUNT] = {0};
  ShadowDrawBatch shadow_user_batches[TB_CASCADE_COUNT] = {0};
  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    const VisibleSet2 *lit_set = &TB_DYN_ARR_AT(lit_sets, i);
    uint32_t mesh_count = TB_DYN_ARR_SIZE(lit_set->meshes);
    // Batch could use each light
    shadow_user_batches[i].views =
        tb_alloc_nm_tp(rnd_tmp_alloc, dir_light_it.count, ShadowDrawView);
    shadow_user_batches[i].view_count = 0;
    shadow_batches[i].user_batch = &shadow_user_batches[i];

    ShadowDrawView *view = &shadow_user_batches[i].views[0];
    // Each view already knows how many meshes it should see
    // Each mesh could have TB_SUBMESH_MAX # of submeshes
    *view = (ShadowDrawView){0};
    view->draws = tb_alloc_nm_tp(rnd_tmp_alloc, mesh_count, ShadowDraw);
    view->draw_count = 0;

    SDL_memset(view->draws, 0, sizeof(ShadowDraw) * mesh_count);
  }

  // TODO: Make this less hacky
  const uint32_t width = rnd_sys->render_thread->swapchain.width;
  const uint32_t height = rnd_sys->render_thread->swapchain.height;

  // Gather prepass, opaque and transparent batches
  for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
    const VisibleSet2 *vis_set = &TB_DYN_ARR_AT(vis_sets, cam_idx);

    // Get camera descriptor set
    VkDescriptorSet view_set =
        tb_view_system_get_descriptor(view_sys, vis_set->view);

    TB_DYN_ARR_FOREACH(vis_set->meshes, mesh_idx) {
      const MeshComponent *mesh_comp = TB_DYN_ARR_AT(vis_set->meshes, mesh_idx);

      // Get mesh descriptor set
      VkDescriptorSet obj_set =
          tb_render_object_system_get_descriptor(ro_sys, mesh_comp->object_id);

      // Organize draws into batches
      {
        // Determine which pipeline is in use
        VkBuffer geom_buffer =
            tb_mesh_system_get_gpu_mesh(mesh_sys, mesh_comp->mesh_id);

        uint32_t opaque_draw_idx = 0;
        uint32_t trans_draw_idx = 0;

        for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
             ++sub_idx) {
          const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];

          TbMaterialPerm mat_perm =
              tb_mat_system_get_perm(mat_sys, submesh->material);
          VkDescriptorSet material_set =
              tb_mat_system_get_set(mat_sys, submesh->material);
          const uint32_t pipe_idx =
              get_pipeline_for_input(mesh_sys, submesh->vertex_input);
          const uint32_t local_pipe_idx = pipe_idxs[pipe_idx];

          bool opaque = true;
          if (mat_perm & GLTF_PERM_ALPHA_CLIP ||
              mat_perm & GLTF_PERM_ALPHA_BLEND) {
            opaque = false;
          }

          // Handle opaque draw
          if (opaque) {
            // Handle prepass draw
            {
              prepass_user_batch->view_count = camera_count;
              MeshDrawView *view = &prepass_user_batch->views[cam_idx];
              view->view_set = view_set;
              view->viewport =
                  (VkViewport){0, height, width, -(float)height, 0, 1};
              view->scissor = (VkRect2D){{0, 0}, {width, height}};
              view->draw_count = TB_DYN_ARR_SIZE(vis_set->meshes);
              MeshDraw *draw = &view->draws[mesh_idx];
              draw->geom_buffer = geom_buffer;
              draw->obj_set = obj_set;
              draw->submesh_draw_count = opaque_draw_idx + 1;
              SubMeshDraw *sub_draw =
                  &view->draws[mesh_idx].submesh_draws[opaque_draw_idx];
              *sub_draw = (SubMeshDraw){
                  .consts = {.perm = mat_perm}, // Need this for
                                                // some last minute
                                                // material options
                  .index_type = submesh->index_type,
                  .index_count = submesh->index_count,
                  .index_offset = submesh->index_offset,
              };

              const uint64_t base_vert_offset = submesh->vertex_offset;
              const uint32_t vertex_count = submesh->vertex_count;

              static const uint64_t pos_stride = sizeof(uint16_t) * 4;

              // We only ever need position and normal vertex
              // attributes
              sub_draw->vertex_binding_count = 2;
              sub_draw->vertex_binding_offsets[0] = base_vert_offset;
              sub_draw->vertex_binding_offsets[1] =
                  base_vert_offset + (vertex_count * pos_stride);
            }

            DrawBatch *opaque_batch = &opaque_batches[local_pipe_idx];
            opaque_batch->pipeline = mesh_sys->opaque_pipelines[pipe_idx];
            opaque_batch->layout = mesh_sys->pipe_layout;

            MeshDrawBatch *mesh_batch = &opaque_user_batches[local_pipe_idx];
            mesh_batch->view_count = camera_count;
            MeshDrawView *view = &mesh_batch->views[cam_idx];
            view->view_set = view_set;
            view->viewport =
                (VkViewport){0, height, width, -(float)height, 0, 1};
            view->scissor = (VkRect2D){{0, 0}, {width, height}};
            view->draw_count = TB_DYN_ARR_SIZE(vis_set->meshes);
            MeshDraw *draw = &view->draws[mesh_idx];
            draw->geom_buffer = geom_buffer;
            draw->obj_set = obj_set;
            draw->submesh_draw_count = opaque_draw_idx + 1;
            SubMeshDraw *sub_draw =
                &view->draws[mesh_idx].submesh_draws[opaque_draw_idx];
            *sub_draw = (SubMeshDraw){
                .mat_set = material_set,
                .consts = {.perm = mat_perm},
                .index_type = submesh->index_type,
                .index_count = submesh->index_count,
                .index_offset = submesh->index_offset,
            };

            const uint64_t base_vert_offset = submesh->vertex_offset;
            const uint32_t vertex_count = submesh->vertex_count;

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
                  base_vert_offset +
                  (vertex_count * (pos_stride + attr_stride));
              break;
            case VI_P3N3T4U2:
              sub_draw->vertex_binding_count = 4;
              sub_draw->vertex_binding_offsets[0] = base_vert_offset;
              sub_draw->vertex_binding_offsets[1] =
                  base_vert_offset + (vertex_count * pos_stride);
              sub_draw->vertex_binding_offsets[2] =
                  base_vert_offset +
                  (vertex_count * (pos_stride + attr_stride));
              sub_draw->vertex_binding_offsets[3] =
                  base_vert_offset +
                  (vertex_count * (pos_stride + (attr_stride * 2)));
              break;
            default:
              TB_CHECK(false, "Unexepcted vertex input");
              break;
            }

            opaque_draw_idx++;
          } else {
            DrawBatch *trans_batch = &transparent_batches[local_pipe_idx];
            trans_batch->pipeline = mesh_sys->transparent_pipelines[pipe_idx];
            trans_batch->layout = mesh_sys->pipe_layout;

            MeshDrawBatch *mesh_batch =
                &transparent_user_batches[local_pipe_idx];
            mesh_batch->view_count = camera_count;
            MeshDrawView *view = &mesh_batch->views[cam_idx];
            view->view_set = view_set;
            view->viewport =
                (VkViewport){0, height, width, -(float)height, 0, 1};
            view->scissor = (VkRect2D){{0, 0}, {width, height}};
            view->draw_count = TB_DYN_ARR_SIZE(vis_set->meshes);
            MeshDraw *draw = &view->draws[mesh_idx];
            draw->geom_buffer = geom_buffer;
            draw->obj_set = obj_set;
            draw->submesh_draw_count = trans_draw_idx + 1;
            SubMeshDraw *sub_draw =
                &view->draws[mesh_idx].submesh_draws[trans_draw_idx];
            *sub_draw = (SubMeshDraw){
                .mat_set = material_set,
                .consts = {.perm = mat_perm},
                .index_type = submesh->index_type,
                .index_count = submesh->index_count,
                .index_offset = submesh->index_offset,
            };

            const uint64_t base_vert_offset = submesh->vertex_offset;
            const uint32_t vertex_count = submesh->vertex_count;

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
                  base_vert_offset +
                  (vertex_count * (pos_stride + attr_stride));
              break;
            case VI_P3N3T4U2:
              sub_draw->vertex_binding_count = 4;
              sub_draw->vertex_binding_offsets[0] = base_vert_offset;
              sub_draw->vertex_binding_offsets[1] =
                  base_vert_offset + (vertex_count * pos_stride);
              sub_draw->vertex_binding_offsets[2] =
                  base_vert_offset +
                  (vertex_count * (pos_stride + attr_stride));
              sub_draw->vertex_binding_offsets[3] =
                  base_vert_offset +
                  (vertex_count * (pos_stride + (attr_stride * 2)));
              break;
            default:
              TB_CHECK(false, "Unexepcted vertex input");
              break;
            }
            trans_draw_idx++;
          }
        }
      }
    }
  }

  // Submit prepass batch
  tb_render_pipeline_issue_draw_batch(rp_sys, mesh_sys->prepass_draw_ctx, 1,
                                      prepass_batch);
  // Submit opaque batches
  tb_render_pipeline_issue_draw_batch(rp_sys, mesh_sys->opaque_draw_ctx,
                                      batch_count, opaque_batches);
  // Submit transparent batches
  tb_render_pipeline_issue_draw_batch(rp_sys, mesh_sys->transparent_draw_ctx,
                                      batch_count, transparent_batches);

  // Similar process for shadow batch
  for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
       ++cascade_idx) {
    const VisibleSet2 *lit_set = &TB_DYN_ARR_AT(lit_sets, cascade_idx);
    uint32_t mesh_count = TB_DYN_ARR_SIZE(lit_set->meshes);
    const View *view = tb_get_view(view_sys, lit_set->view);

    TB_DYN_ARR_FOREACH(lit_set->meshes, mesh_idx) {
      const MeshComponent *mesh_comp = TB_DYN_ARR_AT(lit_set->meshes, mesh_idx);

      VkBuffer geom_buffer =
          tb_mesh_system_get_gpu_mesh(mesh_sys, mesh_comp->mesh_id);
      const CommonObjectData *mesh_data =
          tb_render_object_system_get_data(ro_sys, mesh_comp->object_id);

      uint32_t submesh_draw_idx = 0;

      for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
           ++sub_idx) {
        const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];

        // Still need the material perm to check for things
        // like double-sidedness
        TbMaterialPerm mat_perm =
            tb_mat_system_get_perm(mat_sys, submesh->material);

        shadow_batches[cascade_idx].pipeline = mesh_sys->shadow_pipeline;
        shadow_batches[cascade_idx].layout = mesh_sys->shadow_pipe_layout;
        shadow_user_batches[cascade_idx].view_count = dir_light_it.count;
        ShadowDrawView *draw_view = &shadow_user_batches[cascade_idx].views[0];
        draw_view->viewport = (VkViewport){0,
                                           TB_SHADOW_MAP_DIM * cascade_idx,
                                           TB_SHADOW_MAP_DIM,
                                           TB_SHADOW_MAP_DIM,
                                           0,
                                           1};
        draw_view->scissor = (VkRect2D){{0, TB_SHADOW_MAP_DIM * cascade_idx},
                                        {TB_SHADOW_MAP_DIM, TB_SHADOW_MAP_DIM}};
        draw_view->draw_count = mesh_count;
        draw_view->consts = (ShadowViewConstants){view->view_data.vp};
        ShadowDraw *draw = &draw_view->draws[mesh_idx];
        draw->consts.m = mesh_data->m;
        draw->geom_buffer = geom_buffer;
        draw->submesh_draw_count = submesh_draw_idx + 1;
        ShadowSubDraw *sub_draw =
            &draw_view->draws[mesh_idx].submesh_draws[submesh_draw_idx];
        *sub_draw = (ShadowSubDraw){
            .mat_perm = mat_perm,
            .index_type = submesh->index_type,
            .index_count = submesh->index_count,
            .index_offset = submesh->index_offset,
            .vertex_binding_offset = submesh->vertex_offset,
        };

        submesh_draw_idx++;
      }
    }
  }

  tb_render_pipeline_issue_draw_batch(rp_sys, mesh_sys->shadow_draw_ctx,
                                      TB_CASCADE_COUNT, shadow_batches);

  TracyCZoneEnd(ctx);
}

// Second implementation focused more on instanced draws
/* Pseudocode
  for each camera
    determine which meshes are visible
  for each visible mesh
    group submesh draws by instance
  for each unique submesh
    issue a batch of instanced draws
*/
void mesh_draw_tick2(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Mesh Draw Tick", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, CameraComponent);
  ECS_COMPONENT(ecs, MeshComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, MeshSystem);
  ECS_COMPONENT(ecs, MaterialSystem);
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  ECS_COMPONENT(ecs, RenderObjectSystem);
  ECS_COMPONENT(ecs, ViewSystem);

  MeshSystem *mesh_sys = ecs_field(it, MeshSystem, 1);
  MaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, MaterialSystem);
  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, RenderPipelineSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);

  Allocator tmp_alloc = mesh_sys->tmp_alloc;

  // For each camera
  ecs_iter_t camera_it = ecs_query_iter(ecs, mesh_sys->camera_query);
  while (ecs_query_next(&camera_it)) {
    CameraComponent *cameras = ecs_field(&camera_it, CameraComponent, 1);
    for (int32_t cam_idx = 0; cam_idx < camera_it.count; ++cam_idx) {
      CameraComponent *camera = &cameras[cam_idx];
      VkDescriptorSet view_set =
          tb_view_system_get_descriptor(view_sys, camera->view_id);

      const float width = camera->width;
      const float height = camera->height;

      // Run query to determine how many meshes so we can pre-allocate space for
      // batches
      ecs_iter_t mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
      const uint32_t max_mesh_count = ecs_iter_count(&mesh_it) * TB_SUBMESH_MAX;
      mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);

      // Init arrays on an allocator with a fast allocation
      // so we can inexpensivley append
      // Note that worst case is each mesh needs a separate mesh
      DrawBatchList opaque_batches = {0};
      DrawBatchList trans_batches = {0};
      PrimitiveBatchList opaque_prim_batches = {0};
      PrimitiveBatchList trans_prim_batches = {0};
      TB_DYN_ARR_RESET(opaque_batches, tmp_alloc, max_mesh_count);
      TB_DYN_ARR_RESET(trans_batches, tmp_alloc, max_mesh_count);
      TB_DYN_ARR_RESET(opaque_prim_batches, tmp_alloc, max_mesh_count);
      TB_DYN_ARR_RESET(trans_prim_batches, tmp_alloc, max_mesh_count);
      PrimitiveTransformLists opaque_prim_trans = {0};
      PrimitiveTransformLists trans_prim_trans = {0};
      TB_DYN_ARR_RESET(opaque_prim_trans, tmp_alloc, max_mesh_count);
      TB_DYN_ARR_RESET(trans_prim_trans, tmp_alloc, max_mesh_count);
      TracyCZoneN(ctx2, "Iterate Meshes", true);
      while (ecs_query_next(&mesh_it)) {
        MeshComponent *meshes = ecs_field(&mesh_it, MeshComponent, 1);
        TransformComponent *transforms =
            ecs_field(&mesh_it, TransformComponent, 2);
        for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
          MeshComponent *mesh = &meshes[mesh_idx];
          TransformComponent *trans = &transforms[mesh_idx];

          // Visibility check
          bool visible = true;
          if (!visible) {
            continue;
          }

          VkBuffer geom_buffer =
              tb_mesh_system_get_gpu_mesh(mesh_sys, mesh->mesh_id);

          for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count;
               ++submesh_idx) {
            SubMesh *sm = &mesh->submeshes[submesh_idx];
            TbMaterialId mat = sm->material;
            uint32_t pipe_idx =
                get_pipeline_for_input(mesh_sys, sm->vertex_input);

            // Deduce some important details from the submesh
            TbMaterialPerm perm = tb_mat_system_get_perm(mat_sys, mat);
            VkDescriptorSet mat_set = tb_mat_system_get_set(mat_sys, mat);

            const uint32_t index_count = sm->index_count;
            const uint64_t index_offset = sm->index_offset;
            const uint32_t index_type = sm->index_type;

            // Handle Opaque and Transparent draws
            {
              VkPipelineLayout layout = mesh_sys->pipe_layout;
              bool opaque = true;
              if (perm & GLTF_PERM_ALPHA_CLIP || perm & GLTF_PERM_ALPHA_BLEND) {
                opaque = false;
              }
              VkPipeline pipeline = mesh_sys->opaque_pipelines[pipe_idx];
              if (!opaque) {
                pipeline = mesh_sys->transparent_pipelines[pipe_idx];
              }

              // Determine if we need to insert a new batch
              DrawBatch *batch = NULL;
              PrimitiveBatch *prim_batch = NULL;
              TransformList *transforms = NULL;
              {
                DrawBatchList *batches = &opaque_batches;
                PrimitiveBatchList *prim_batches = &opaque_prim_batches;
                PrimitiveTransformLists *trans_list = &opaque_prim_trans;
                if (!opaque) {
                  batches = &trans_batches;
                  prim_batches = &trans_prim_batches;
                  trans_list = &trans_prim_trans;
                }

                // Try to find an existing suitable batch
                TB_DYN_ARR_FOREACH(*batches, i) {
                  DrawBatch *db = &TB_DYN_ARR_AT(*batches, i);
                  PrimitiveBatch *pb = &TB_DYN_ARR_AT(*prim_batches, i);
                  TransformList *tl = &TB_DYN_ARR_AT(*trans_list, i);
                  if (db->pipeline == pipeline && db->layout == layout &&
                      pb->perm == perm && pb->view_set == view_set &&
                      pb->mat_set == mat_set &&
                      pb->geom_buffer == geom_buffer) {
                    batch = db;
                    prim_batch = pb;
                    transforms = tl;
                    break;
                  }
                }
                // No batch was found, create one
                if (batch == NULL) {
                  // Worst case batch count is one batch having to carry every
                  // mesh with the maximum number of possible submeshes
                  const uint32_t max_draw_count = max_mesh_count;
                  DrawBatch db = {
                      .pipeline = pipeline,
                      .layout = layout,
                      .viewport = {0, height, width, -(float)height, 0, 1},
                      .scissor = (VkRect2D){{0, 0}, {width, height}},
                      .draw_size = sizeof(PrimitiveDraw),
                      .draws = tb_alloc_nm_tp(tmp_alloc, max_draw_count,
                                              PrimitiveDraw),
                  };
                  PrimitiveBatch pb = {.perm = perm,
                                       .view_set = view_set,
                                       .mat_set = mat_set,
                                       .geom_buffer = geom_buffer};

                  TransformList tl = {0};
                  TB_DYN_ARR_RESET(tl, tmp_alloc, max_draw_count);

                  // Append it to the list and make sure we get a reference
                  uint32_t idx = TB_DYN_ARR_SIZE(*prim_batches);
                  TB_DYN_ARR_APPEND(*prim_batches, pb);
                  prim_batch = &TB_DYN_ARR_AT(*prim_batches, idx);
                  TB_DYN_ARR_APPEND(*batches, db);
                  batch = &TB_DYN_ARR_AT(*batches, idx);
                  TB_DYN_ARR_APPEND(*trans_list, tl);
                  transforms = &TB_DYN_ARR_AT(*trans_list, idx);

                  batch->user_batch = prim_batch;
                }
              }

              // Determine if we need to insert a new draw
              {
                PrimitiveDraw *draw = NULL;
                for (uint32_t i = 0; i < batch->draw_count; ++i) {
                  PrimitiveDraw *d = &((PrimitiveDraw *)batch->draws)[i];
                  if (d->index_count == index_count &&
                      d->index_offset == index_offset &&
                      d->index_type == index_type) {
                    draw = d;
                    break;
                  }
                }
                // No draw was found, create one
                if (draw == NULL) {
                  PrimitiveDraw d = {
                      .index_count = index_count,
                      .index_offset = index_offset,
                      .index_type = index_type,
                  };

                  const uint64_t base_vert_offset = sm->vertex_offset;
                  const uint32_t vertex_count = sm->vertex_count;

                  switch (sm->vertex_input) {
                  case VI_P3N3:
                    d.vertex_binding_count = 2;
                    d.vertex_binding_offsets[0] = base_vert_offset;
                    d.vertex_binding_offsets[1] =
                        base_vert_offset + (vertex_count * pos_stride);
                    break;
                  case VI_P3N3U2:
                    d.vertex_binding_count = 3;
                    d.vertex_binding_offsets[0] = base_vert_offset;
                    d.vertex_binding_offsets[1] =
                        base_vert_offset + (vertex_count * pos_stride);
                    d.vertex_binding_offsets[2] =
                        base_vert_offset +
                        (vertex_count * (pos_stride + attr_stride));
                    break;
                  case VI_P3N3T4U2:
                    d.vertex_binding_count = 4;
                    d.vertex_binding_offsets[0] = base_vert_offset;
                    d.vertex_binding_offsets[1] =
                        base_vert_offset + (vertex_count * pos_stride);
                    d.vertex_binding_offsets[2] =
                        base_vert_offset +
                        (vertex_count * (pos_stride + attr_stride));
                    d.vertex_binding_offsets[3] =
                        base_vert_offset +
                        (vertex_count * (pos_stride + (attr_stride * 2)));
                    break;
                  default:
                    TB_CHECK(false, "Unexepcted vertex input");
                    break;
                  }

                  // Append it to the list and make sure we get a reference
                  uint32_t idx = batch->draw_count++;
                  ((PrimitiveDraw *)batch->draws)[idx] = d;
                  draw = &((PrimitiveDraw *)batch->draws)[idx];
                }
                draw->instance_count++;
                // Append this transform to the transform list for the batch
                float4x4 mat = tb_transform_get_world_matrix(ecs, trans);
                TB_DYN_ARR_APPEND(*transforms, mat);
              }
            }
          }
        }
      }
      TracyCZoneEnd(ctx2);

      // Establish prepass batches by making a batch for each opaque
      // batch but with a different pipeline and the same primitive batch
      DrawBatchList prepass_batches = {0};
      TB_DYN_ARR_RESET(prepass_batches, tmp_alloc,
                       TB_DYN_ARR_SIZE(opaque_batches));
      {
        TracyCZoneN(ctx2, "Handle Prepass", true);
        VkPipelineLayout layout = mesh_sys->prepass_layout;
        VkPipeline pipeline = mesh_sys->prepass_pipe;

        TB_DYN_ARR_FOREACH(opaque_batches, i) {
          const DrawBatch *op_batch = &TB_DYN_ARR_AT(opaque_batches, i);

          DrawBatch pre_batch = *op_batch;
          pre_batch.pipeline = pipeline;
          pre_batch.layout = layout;

          TB_DYN_ARR_APPEND(prepass_batches, pre_batch);
        }
        TracyCZoneEnd(ctx2);
      }

      // Write transform lists to the GPU temp buffer
      TB_DYN_ARR_OF(TbHostBuffer) opaque_inst_buffers = {0};
      TB_DYN_ARR_OF(TbHostBuffer) trans_inst_buffers = {0};
      {
        TracyCZoneN(ctx2, "Gather Transforms", true);
        const uint32_t op_count = TB_DYN_ARR_SIZE(opaque_prim_trans);
        if (op_count) {
          TB_DYN_ARR_RESET(opaque_inst_buffers, tmp_alloc, op_count);

          TB_DYN_ARR_FOREACH(opaque_prim_trans, i) {
            TransformList *transforms = &TB_DYN_ARR_AT(opaque_prim_trans, i);

            const size_t trans_size =
                sizeof(float4x4) * TB_DYN_ARR_SIZE(*transforms);

            TbHostBuffer host_buffer = {0};
            tb_rnd_sys_alloc_tmp_host_buffer(rnd_sys, trans_size, 0x40,
                                             &host_buffer);
            SDL_memcpy(host_buffer.ptr, transforms->data, trans_size);

            TB_DYN_ARR_APPEND(opaque_inst_buffers, host_buffer);
          }
        }

        const uint32_t trans_count = TB_DYN_ARR_SIZE(trans_prim_trans);
        if (trans_count) {
          TB_DYN_ARR_RESET(trans_inst_buffers, tmp_alloc, trans_count);

          TB_DYN_ARR_FOREACH(trans_prim_trans, i) {
            TransformList *transforms = &TB_DYN_ARR_AT(trans_prim_trans, i);

            const size_t trans_size =
                sizeof(float4x4) * TB_DYN_ARR_SIZE(*transforms);

            TbHostBuffer host_buffer = {0};
            tb_rnd_sys_alloc_tmp_host_buffer(rnd_sys, trans_size, 0x40,
                                             &host_buffer);
            SDL_memcpy(host_buffer.ptr, transforms->data, trans_size);

            TB_DYN_ARR_APPEND(trans_inst_buffers, host_buffer);
          }
        }
        TracyCZoneEnd(ctx2);
      }

      // Alloc and write transform descriptor sets
      {
        TracyCZoneN(ctx2, "Write Descriptors", true);
        const uint32_t set_count = TB_DYN_ARR_SIZE(opaque_inst_buffers) +
                                   TB_DYN_ARR_SIZE(trans_inst_buffers);
        VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = set_count * 4,
            .poolSizeCount = 1,
            .pPoolSizes =
                (VkDescriptorPoolSize[1]){
                    {
                        .descriptorCount = set_count * 4,
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    },
                },
        };
        VkDescriptorSetLayout *layouts =
            tb_alloc_nm_tp(tmp_alloc, set_count, VkDescriptorSetLayout);
        for (uint32_t i = 0; i < set_count; ++i) {
          layouts[i] = mesh_sys->obj_set_layout;
        }
        VkResult err = tb_rnd_frame_desc_pool_tick(
            rnd_sys, &pool_info, layouts, mesh_sys->desc_pool_list.pools,
            set_count);
        TB_VK_CHECK(err, "Failed to update descriptor pool");

        VkBuffer gpu_buf = tb_rnd_get_gpu_tmp_buffer(rnd_sys);

        // Get and write a buffer to each descriptor set
        VkWriteDescriptorSet *writes =
            tb_alloc_nm_tp(tmp_alloc, set_count, VkWriteDescriptorSet);

        uint32_t set_idx = 0;
        TB_DYN_ARR_FOREACH(opaque_inst_buffers, i) {
          VkDescriptorSet set = tb_rnd_frame_desc_pool_get_set(
              rnd_sys, mesh_sys->desc_pool_list.pools, set_idx);
          const uint64_t offset = TB_DYN_ARR_AT(opaque_inst_buffers, i).offset;
          TransformList *transforms = &TB_DYN_ARR_AT(opaque_prim_trans, i);
          const uint64_t trans_count = TB_DYN_ARR_SIZE(*transforms);

          VkDescriptorBufferInfo *buffer_info =
              tb_alloc_tp(tmp_alloc, VkDescriptorBufferInfo);
          *buffer_info = (VkDescriptorBufferInfo){
              .buffer = gpu_buf,
              .offset = offset,
              .range = sizeof(float4x4) * trans_count,
          };

          writes[set_idx++] = (VkWriteDescriptorSet){
              .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = set,
              .dstBinding = 0,
              .dstArrayElement = 0,
              .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .pBufferInfo = buffer_info,
          };

          // Need to make sure the batch is also using the correct set
          TB_DYN_ARR_AT(opaque_prim_batches, i).inst_set = set;
        }
        TB_DYN_ARR_FOREACH(trans_inst_buffers, i) {
          VkDescriptorSet set = tb_rnd_frame_desc_pool_get_set(
              rnd_sys, mesh_sys->desc_pool_list.pools, set_idx);
          const uint64_t offset = TB_DYN_ARR_AT(trans_inst_buffers, i).offset;
          TransformList *transforms = &TB_DYN_ARR_AT(trans_prim_trans, i);
          const uint64_t trans_count = TB_DYN_ARR_SIZE(*transforms);

          VkDescriptorBufferInfo *buffer_info =
              tb_alloc_tp(tmp_alloc, VkDescriptorBufferInfo);
          *buffer_info = (VkDescriptorBufferInfo){
              .buffer = gpu_buf,
              .offset = offset,
              .range = sizeof(float4x4) * trans_count,
          };

          writes[set_idx++] = (VkWriteDescriptorSet){
              .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = set,
              .dstBinding = 0,
              .dstArrayElement = 0,
              .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .pBufferInfo = buffer_info,
          };

          // Need to make sure the batch is also using the correct set
          TB_DYN_ARR_AT(trans_prim_batches, i).inst_set = set;
        }

        tb_rnd_update_descriptors(rnd_sys, set_count, writes);
        TracyCZoneEnd(ctx2);
      }

      // Submit batches
      {
        TracyCZoneN(ctx2, "Submit Batches", true);
        TbDrawContextId prepass_ctx2 = mesh_sys->prepass_draw_ctx2;
        TbDrawContextId opaque_ctx2 = mesh_sys->opaque_draw_ctx2;
        TbDrawContextId trans_ctx2 = mesh_sys->transparent_draw_ctx2;

        // For prepass
        tb_render_pipeline_issue_draw_batch(rp_sys, prepass_ctx2,
                                            TB_DYN_ARR_SIZE(prepass_batches),
                                            prepass_batches.data);
        // For opaque pass
        tb_render_pipeline_issue_draw_batch(rp_sys, opaque_ctx2,
                                            TB_DYN_ARR_SIZE(opaque_batches),
                                            opaque_batches.data);
        // For transparent pass
        tb_render_pipeline_issue_draw_batch(rp_sys, trans_ctx2,
                                            TB_DYN_ARR_SIZE(trans_batches),
                                            opaque_batches.data);
        TracyCZoneEnd(ctx2);
      }
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_register_mesh_sys(ecs_world_t *ecs, Allocator std_alloc,
                          Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, MaterialSystem);
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, RenderObjectSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  ECS_COMPONENT(ecs, MeshComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, CameraComponent);
  ECS_COMPONENT(ecs, MeshSystem);
  ECS_COMPONENT(ecs, AssetSystem);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  MaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, MaterialSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  RenderObjectSystem *ro_sys = ecs_singleton_get_mut(ecs, RenderObjectSystem);
  RenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, RenderPipelineSystem);

  MeshSystem sys = create_mesh_system_internal(
      std_alloc, tmp_alloc, rnd_sys, mat_sys, view_sys, ro_sys, rp_sys);
  sys.camera_query = ecs_query(ecs, {.filter.terms = {
                                         {.id = ecs_id(CameraComponent)},
                                     }});
  sys.mesh_query =
      ecs_query(ecs, {
                         .filter.terms =
                             {
                                 {.id = ecs_id(MeshComponent)},
                                 {.id = ecs_id(TransformComponent)},
                             },
                     });
  sys.dir_light_query =
      ecs_query(ecs, {.filter.terms = {
                          {.id = ecs_id(DirectionalLightComponent)},
                      }});

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(MeshSystem), MeshSystem, &sys);

  // ECS_SYSTEM(ecs, mesh_draw_tick, EcsOnUpdate, CameraComponent,
  //            TransformComponent);

  ECS_SYSTEM(ecs, mesh_draw_tick2, EcsOnUpdate, MeshSystem(MeshSystem));

  tb_register_mesh_component(ecs);
}

void tb_unregister_mesh_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, MeshSystem);
  MeshSystem *sys = ecs_singleton_get_mut(ecs, MeshSystem);
  ecs_query_fini(sys->dir_light_query);
  ecs_query_fini(sys->mesh_query);
  ecs_query_fini(sys->camera_query);
  destroy_mesh_system(sys);
  ecs_singleton_remove(ecs, MeshSystem);
}
