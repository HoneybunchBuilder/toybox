#include "tb_ocean_system.h"

#include "ocean.hlsli"
#include "tb_assets.h"
#include "tb_audio_system.h"
#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_light_component.h"
#include "tb_mesh_rnd_sys.h"
#include "tb_mesh_system.h"
#include "tb_ocean_component.h"
#include "tb_profiling.h"
#include "tb_rand.h"
#include "tb_render_pipeline_system.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_shader_system.h"
#include "tb_task_scheduler.h"
#include "tb_transform_component.h"
#include "tb_util.h"
#include "tb_view_system.h"
#include "tb_visual_logging_system.h"
#include "tb_world.h"

#include <flecs.h>

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "ocean_frag.h"
#include "ocean_vert.h"
#include "oceanprepass_frag.h"
#include "oceanprepass_vert.h"
#pragma clang diagnostic pop

typedef struct OceanDrawBatch {
  VkDescriptorSet view_set;
  VkDescriptorSet ocean_set;
  OceanPushConstants consts;
  VkBuffer inst_buffer;
  uint32_t inst_offset;
  uint32_t inst_count;
  VkBuffer geom_buffer;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t pos_offset;
} OceanDrawBatch;

#define TB_OCEAN_SFX_COUNT 4

typedef struct TbOceanSystem {
  TbRenderSystem *rnd_sys;
  TbRenderPipelineSystem *rp_sys;
  TbMeshSystem *mesh_system;
  TbViewSystem *view_sys;
  TbRenderTargetSystem *rt_sys;
  TbVisualLoggingSystem *vlog;
  TbAudioSystem *audio_system;
  TbAllocator tmp_alloc;
  TbAllocator gp_alloc;

  ecs_query_t *ocean_query;

  TbMusicId music;
  TbSoundEffectId wave_sounds[TB_OCEAN_SFX_COUNT];
  float wave_sound_timer;

  TbMesh2 ocean_patch_mesh2;
  TbTransform ocean_transform;
  float tile_width;
  float tile_depth;
  uint32_t ocean_index_type;
  uint32_t ocean_index_count;
  uint64_t ocean_pos_offset;
  uint64_t ocean_uv_offset;

  VkSampler sampler;
  VkSampler shadow_sampler;

  TbDrawContextId trans_depth_draw_ctx;
  TbDrawContextId trans_color_draw_ctx;

  TbFrameDescriptorPool ocean_pools[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;

  ecs_entity_t ocean_pass_shader;
  ecs_entity_t ocean_prepass_shader;
} TbOceanSystem;
ECS_COMPONENT_DECLARE(TbOceanSystem);

void tb_register_ocean_sys(TbWorld *world);
void tb_unregister_ocean_sys(TbWorld *world);

TB_REGISTER_SYS(tb, ocean, TB_OCEAN_SYS_PRIO)

void ocean_record(VkCommandBuffer buffer, uint32_t batch_count,
                  const TbDrawBatch *batches) {
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    tb_auto batch = &batches[batch_idx];
    tb_auto ocean_batch = (const OceanDrawBatch *)batch->user_batch;
    VkPipelineLayout layout = batch->layout;
    VkBuffer geom_buffer = ocean_batch->geom_buffer;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            1, &ocean_batch->ocean_set, 0, NULL);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                            1, &ocean_batch->view_set, 0, NULL);
    vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(OceanPushConstants), &ocean_batch->consts);

    vkCmdBindIndexBuffer(buffer, geom_buffer, 0, ocean_batch->index_type);
    vkCmdBindVertexBuffers(
        buffer, 0, 2, (VkBuffer[2]){geom_buffer, ocean_batch->inst_buffer},
        (VkDeviceSize[2]){ocean_batch->pos_offset, ocean_batch->inst_offset});

    vkCmdDrawIndexed(buffer, ocean_batch->index_count, ocean_batch->inst_count,
                     0, 0, 0);
  }
}

void ocean_prepass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                          uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Ocean Prepass Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean Prepass", 2, true);
  cmd_begin_label(buffer, "Ocean Prepass", tb_f4(0.0f, 0.4f, 0.4f, 1.0f));

  ocean_record(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void ocean_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Ocean Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean", 2, true);
  cmd_begin_label(buffer, "Ocean", tb_f4(0.0f, 0.8f, 0.8f, 1.0f));

  ocean_record(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

typedef struct TbOceanPipelineArgs {
  TbRenderSystem *rnd_sys;
  VkFormat color_format;
  VkFormat depth_format;
  VkPipelineLayout pipe_layout;
} TbOceanPipelineArgs;

VkPipeline create_ocean_prepass_shader(const TbOceanPipelineArgs *args) {
  VkResult err = VK_SUCCESS;

  tb_auto rnd_sys = args->rnd_sys;
  tb_auto depth_format = args->depth_format;
  tb_auto pipe_layout = args->pipe_layout;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(oceanprepass_vert);
    create_info.pCode = (const uint32_t *)oceanprepass_vert;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Ocean Prepass Vert",
                               &vert_mod);
    TB_VK_CHECK(err, "Failed to load ocean vert shader module");

    create_info.codeSize = sizeof(oceanprepass_frag);
    create_info.pCode = (const uint32_t *)oceanprepass_frag;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Ocean Prepss Frag",
                               &frag_mod);
    TB_VK_CHECK(err, "Failed to load ocean frag shader module");
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
              .vertexBindingDescriptionCount = 2,
              .pVertexBindingDescriptions =
                  (VkVertexInputBindingDescription[2]){
                      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
                      {1, sizeof(float4), VK_VERTEX_INPUT_RATE_INSTANCE},
                  },
              .vertexAttributeDescriptionCount = 2,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[2]){
                      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
                      {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
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
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = pipe_layout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  err = tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                         "Ocean Prepass Pipeline", &pipeline);
  TB_VK_CHECK(err, "Failed to create ocean prepass pipeline");

  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

VkPipeline create_ocean_pass_shader(const TbOceanPipelineArgs *args) {
  VkResult err = VK_SUCCESS;

  tb_auto rnd_sys = args->rnd_sys;
  tb_auto color_format = args->color_format;
  tb_auto depth_format = args->depth_format;
  tb_auto pipe_layout = args->pipe_layout;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

    create_info.codeSize = sizeof(ocean_vert);
    create_info.pCode = (const uint32_t *)ocean_vert;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Ocean Vert", &vert_mod);
    TB_VK_CHECK(err, "Failed to load ocean vert shader module");

    create_info.codeSize = sizeof(ocean_frag);
    create_info.pCode = (const uint32_t *)ocean_frag;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Ocean Frag", &frag_mod);
    TB_VK_CHECK(err, "Failed to load ocean frag shader module");
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
              .vertexBindingDescriptionCount = 2,
              .pVertexBindingDescriptions =
                  (VkVertexInputBindingDescription[2]){
                      {0, sizeof(uint16_t) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
                      {1, sizeof(float4), VK_VERTEX_INPUT_RATE_INSTANCE},
                  },
              .vertexAttributeDescriptionCount = 2,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[2]){
                      {0, 0, VK_FORMAT_R16G16B16A16_SINT, 0},
                      {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
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
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
#endif
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
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  err = tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                         "Ocean Pipeline", &pipeline);
  TB_VK_CHECK(err, "Failed to create ocean pipeline");

  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

void init_ocean_system(ecs_world_t *ecs, TbOceanSystem *sys,
                       TbAllocator gp_alloc, TbAllocator tmp_alloc,
                       TbRenderSystem *rnd_sys, TbRenderPipelineSystem *rp_sys,
                       TbMeshSystem *mesh_system, TbViewSystem *view_sys,
                       TbRenderTargetSystem *rt_sys,
                       TbVisualLoggingSystem *vlog,
                       TbAudioSystem *audio_system) {
  sys->gp_alloc = gp_alloc;
  sys->tmp_alloc = tmp_alloc;
  sys->rnd_sys = rnd_sys;
  sys->rp_sys = rp_sys;
  sys->mesh_system = mesh_system;
  sys->view_sys = view_sys;
  sys->rt_sys = rt_sys;
  sys->vlog = vlog;
  sys->audio_system = audio_system;

  // Load sound effects
  {
    char file_name[100] = {0};
    for (uint32_t i = 0; i < 4; ++i) {
      SDL_snprintf(file_name, 100, "audio/wave0%d.wav", i + 1);
      char *wave_path = tb_resolve_asset_path(sys->tmp_alloc, file_name);
      sys->wave_sounds[i] =
          tb_audio_system_load_effect(sys->audio_system, wave_path);
    }
  }

  // Load the known glb that has the ocean mesh
  // Get qualified path to scene asset
  char *asset_path =
      tb_resolve_asset_path(sys->tmp_alloc, "scenes/ocean_patch.glb");

  // Load glb off disk
  cgltf_data *data = tb_read_glb(sys->gp_alloc, asset_path);
  TB_CHECK(data, "Failed to load glb");

  // Parse expected mesh from glb
  {
    cgltf_mesh *ocean_mesh = &data->meshes[0];
    // Must put mesh name on gp_alloc for proper cleanup
    {
      const char *static_name = "Ocean";
      char *name = tb_alloc_nm_tp(sys->gp_alloc, sizeof(static_name) + 1, char);
      SDL_snprintf(name, sizeof(static_name), "%s", static_name);
      ocean_mesh->name = name;
    }

    sys->ocean_transform = tb_transform_from_node(&data->nodes[0]);

    // Determine mesh's width and height

    {
      const cgltf_primitive *prim = &ocean_mesh->primitives[0];

      uint32_t pos_idx = 0;
      for (uint32_t attr_idx = 0; attr_idx < (uint32_t)prim->attributes_count;
           ++attr_idx) {
        cgltf_attribute_type attr_type = prim->attributes[attr_idx].type;
        if (attr_type == cgltf_attribute_type_position) {
          pos_idx = attr_idx;
          break;
        }
      }
      const cgltf_attribute *pos_attr = &prim->attributes[pos_idx];

      TB_CHECK(pos_attr->type == cgltf_attribute_type_position,
               "Unexpected vertex attribute type");

      float *min = pos_attr->data->min;
      float *max = pos_attr->data->max;

      TbAABB local_aabb = tb_aabb_init();
      tb_aabb_add_point(&local_aabb, tb_f3(min[0], min[1], min[2]));
      tb_aabb_add_point(&local_aabb, tb_f3(max[0], max[1], max[2]));

      float4x4 m = tb_transform_to_matrix(&sys->ocean_transform);
      local_aabb = tb_aabb_transform(m, local_aabb);

      sys->tile_width = tb_aabb_get_width(local_aabb);
      sys->tile_depth = tb_aabb_get_depth(local_aabb);
    }

    sys->ocean_index_type = ocean_mesh->primitives->indices->stride == 2
                                ? VK_INDEX_TYPE_UINT16
                                : VK_INDEX_TYPE_UINT32;
    sys->ocean_index_count = ocean_mesh->primitives->indices->count;

    uint64_t index_size = tb_calc_aligned_size(
        sys->ocean_index_count,
        sys->ocean_index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4, 16);
    sys->ocean_pos_offset = index_size;

    sys->ocean_patch_mesh2 =
        tb_mesh_sys_load_gltf_mesh(ecs, data, asset_path, "ocean", 0);
  }

  // cgltf_free(data);

  VkResult err = VK_SUCCESS;

  // Create Immutable Sampler
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .maxLod = 1.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    err = tb_rnd_create_sampler(rnd_sys, &create_info, "Ocean Sampler",
                                &sys->sampler);
    TB_VK_CHECK(err, "Failed to create ocean sampler");
  }
  // Create immutable sampler for shadows
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .maxLod = 1.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    err = tb_rnd_create_sampler(rnd_sys, &create_info, "Ocean Shadow Sampler",
                                &sys->shadow_sampler);
    TB_VK_CHECK(err, "Failed to create ocean shadow sampler");
  }

  // Create ocean descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 5,
        .pBindings = (VkDescriptorSetLayoutBinding[5]){
            {
                .binding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
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
                .pImmutableSamplers = &sys->sampler,
            },
            {
                .binding = 4,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = &sys->shadow_sampler,
            },
        }};
    err = tb_rnd_create_set_layout(
        rnd_sys, &create_info, "Ocean Descriptor Set Layout", &sys->set_layout);
    TB_VK_CHECK(err, "Failed to create ocean descriptor set layout");
  }

  // Create ocean pipeline layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts =
            (VkDescriptorSetLayout[2]){
                sys->set_layout,
                tb_view_sys_get_set_layout(ecs),
            },
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (VkPushConstantRange[1]){
                {
                    .size = sizeof(OceanPushConstants),
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                },
            },
    };
    err = tb_rnd_create_pipeline_layout(
        rnd_sys, &create_info, "Ocean Pipeline Layout", &sys->pipe_layout);
    TB_VK_CHECK(err, "Failed to create ocean pipeline layout");
  }

  // Retrieve passes
  TbRenderPassId depth_id = sys->rp_sys->transparent_depth_pass;
  TbRenderPassId color_id = sys->rp_sys->transparent_color_pass;

  // Create shader pipelines
  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(
        sys->rp_sys, sys->rp_sys->transparent_depth_pass, &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexpected");
    TbPassAttachment depth_info = {0};
    tb_render_pipeline_get_attachments(sys->rp_sys,
                                       sys->rp_sys->transparent_depth_pass,
                                       &attach_count, &depth_info);

    VkFormat depth_format =
        tb_render_target_get_format(sys->rp_sys->rt_sys, depth_info.attachment);

    VkFormat color_format = VK_FORMAT_UNDEFINED;
    tb_render_pipeline_get_attachments(
        sys->rp_sys, sys->rp_sys->transparent_color_pass, &attach_count, NULL);
    TB_CHECK(attach_count == 2, "Unexpected");
    TbPassAttachment attach_info[2] = {0};
    tb_render_pipeline_get_attachments(sys->rp_sys,
                                       sys->rp_sys->transparent_color_pass,
                                       &attach_count, attach_info);

    for (uint32_t i = 0; i < attach_count; i++) {
      VkFormat format = tb_render_target_get_format(sys->rp_sys->rt_sys,
                                                    attach_info[i].attachment);
      if (format != VK_FORMAT_D32_SFLOAT) {
        color_format = format;
        break;
      }
    }

    // Async load shaders
    {
      TbOceanPipelineArgs args = {
          .rnd_sys = rnd_sys,
          .color_format = color_format,
          .depth_format = depth_format,
          .pipe_layout = sys->pipe_layout,
      };
      sys->ocean_pass_shader =
          tb_shader_load(ecs, (TbShaderCompileFn)&create_ocean_pass_shader,
                         (void *)&args, sizeof(TbOceanPipelineArgs));
      sys->ocean_prepass_shader =
          tb_shader_load(ecs, (TbShaderCompileFn)&create_ocean_prepass_shader,
                         (void *)&args, sizeof(TbOceanPipelineArgs));
    }
  }

  sys->trans_depth_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(OceanDrawBatch),
                  .draw_fn = ocean_prepass_record,
                  .pass_id = depth_id,
              });
  sys->trans_color_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(OceanDrawBatch),
                  .draw_fn = ocean_pass_record,
                  .pass_id = color_id,
              });
}

void destroy_ocean_system(TbOceanSystem *self) {
  for (uint32_t i = 0; i < 4; ++i) {
    tb_audio_system_release_effect_ref(self->audio_system,
                                       self->wave_sounds[i]);
  }
  // tb_audio_system_release_music_ref(self->audio_system, self->music);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->rnd_sys,
                                   self->ocean_pools[i].set_pool);
  }

  tb_rnd_destroy_sampler(self->rnd_sys, self->sampler);
  tb_rnd_destroy_sampler(self->rnd_sys, self->shadow_sampler);

  tb_rnd_destroy_pipe_layout(self->rnd_sys, self->pipe_layout);
  tb_rnd_destroy_set_layout(self->rnd_sys, self->set_layout);

  *self = (TbOceanSystem){0};
}

void ocean_audio_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Ocean Audio System", TracyCategoryColorAudio, true);

  tb_auto sys = ecs_field(it, TbOceanSystem, 1);
  tb_auto components = ecs_field(it, TbOceanComponent, 2);

  if (it->count > 0) {
    (void)components;
    sys->wave_sound_timer -= it->delta_time;
    if (sys->wave_sound_timer <= 0.0f) {
      sys->wave_sound_timer = tb_rand_rangef(1.3f, 2.0f);

      uint64_t idx = tb_rand() % TB_OCEAN_SFX_COUNT;
      tb_audio_play_effect(sys->audio_system, sys->wave_sounds[idx]);
    }
  }

  TracyCZoneEnd(ctx);
}

void ocean_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Ocean Draw System", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = it->world;

  double time = ecs_singleton_get(it->world, TbWorldRef)->world->time;
  tb_auto sys = ecs_field(it, TbOceanSystem, 1);
  tb_auto cameras = ecs_field(it, TbCameraComponent, 2);

  // If shaders aren't ready just bail
  if (!tb_is_shader_ready(ecs, sys->ocean_pass_shader) ||
      !tb_is_shader_ready(ecs, sys->ocean_prepass_shader)) {
    TracyCZoneEnd(ctx);
    return;
  }

  // If mesh isn't loaded just bail
  if (!tb_is_mesh_ready(ecs, sys->ocean_patch_mesh2)) {
    TracyCZoneEnd(ctx);
    return;
  }

  tb_auto rnd_sys = sys->rnd_sys;
  tb_auto view_sys = sys->view_sys;

  for (int32_t i = 0; i < it->count; ++i) {
    TbCameraComponent *camera = &cameras[i];

    const uint32_t width = camera->width;
    const uint32_t height = camera->height;

    VkResult err = VK_SUCCESS;

    tb_auto view_set = tb_view_system_get_descriptor(view_sys, camera->view_id);
    // Skip camera if view set isn't ready
    if (view_set == VK_NULL_HANDLE) {
      continue;
    }

    // We want to draw a number of ocean tiles to cover the entire ocean plane
    // Since only visible ocean tiles need to be drawn we can calculate the
    // tiles relative to the view

    // Get the camera's view so we can examine its frustum and decide where to
    // place ocean tiles
    const TbView *view = tb_get_view(sys->view_sys, camera->view_id);
    float4x4 inv_v = tb_invf44(view->view_data.v);

    // Get frustum TbAABB in view space by taking a unit frustum and
    // transforming it by the view's projection
    TbAABB frust_aabb = tb_aabb_init();
    {
      float3 frustum_corners[TB_FRUSTUM_CORNER_COUNT] = {{0}};
      for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; ++i) {
        float3 corner = tb_frustum_corners[i];
        // TbTransform from screen space to world space
        float4 inv_corner =
            tb_mulf44f4(view->view_data.inv_vp, tb_f3tof4(corner, 1.0f));
        frustum_corners[i] = tb_f4tof3(inv_corner) / inv_corner.w;
        frustum_corners[i][TB_HEIGHT_IDX] = 0.0f; // Flatten the TbAABB
        tb_aabb_add_point(&frust_aabb, frustum_corners[i]);
      }
    }

    // Determine how many tiles we'll need
    uint32_t tile_count = 0;
    uint32_t horiz_tile_count = 0;
    uint32_t deep_tile_count = 0;
    {
      float frust_width = tb_aabb_get_width(frust_aabb);
      float frust_depth = tb_aabb_get_depth(frust_aabb);

      horiz_tile_count = (uint32_t)SDL_ceilf(frust_width / sys->tile_width);
      deep_tile_count = (uint32_t)SDL_ceilf(frust_depth / sys->tile_depth);
      tile_count = horiz_tile_count * deep_tile_count;
    }

    // See which of these tiles pass the visibility check against the camera
    uint32_t visible_tile_count = 0;
    // Worst case the projection is orthographic and all tiles are visible
    // That allocation is quick to make up front on the temp allocator
    float4 *visible_tile_offsets =
        tb_alloc_nm_tp(sys->tmp_alloc, tile_count, float4);
    {
      float half_width = sys->tile_width * 0.5f;
      float half_depth = sys->tile_depth * 0.5f;
      float4 pos = {
          frust_aabb.min[TB_WIDTH_IDX] + half_width,
          0,
          frust_aabb.min[TB_DEPTH_IDX] + half_depth,
          0,
      };

      float3 view_to_world_offset = tb_f4tof3(inv_v.col3);
      view_to_world_offset[TB_HEIGHT_IDX] = 0.0f;

      for (uint32_t d = 0; d < deep_tile_count; ++d) {
        for (uint32_t h = 0; h < horiz_tile_count; ++h) {
          TbAABB world_aabb = tb_aabb_init();
          float3 min = tb_f3(-half_width, 0, -half_depth) + pos.xyz;
          float3 max = tb_f3(half_width, 0, half_depth) + pos.xyz;

          tb_aabb_add_point(&world_aabb, min + view_to_world_offset);
          tb_aabb_add_point(&world_aabb, max + view_to_world_offset);

          // TODO: Make frustum test more reliable
          // if (frustum_test_aabb(&view->frustum, &world_aabb))
          {
            float3 offset = pos.xyz;
            offset[TB_HEIGHT_IDX] = 0.0f;
            // tb_vlog_location(self->vlog, offset, 20.0f, f3(0, 1, 0));
            visible_tile_offsets[visible_tile_count++] =
                tb_f3tof4(offset, 0.0f);
          }
          // else {
          //   tb_vlog_location(self->vlog, pos, 20.0f, f3(1, 0, 0));
          // }
          pos[TB_WIDTH_IDX] += sys->tile_width;
        }
        pos[TB_WIDTH_IDX] = frust_aabb.min[TB_WIDTH_IDX] + half_width,
        pos[TB_DEPTH_IDX] += sys->tile_depth;
      }
    }
    // Now that all the tile offsets are calculated, move them on to the tmp
    // gpu which we know will get uploaded and record the offset
    uint64_t tile_offset = 0;
    {
      uint64_t size = sizeof(float4) * visible_tile_count;
      err = tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, size, 0x40,
                                          visible_tile_offsets, &tile_offset);
      TB_VK_CHECK(err, "Failed to allocate ocean instance buffer");
    }

    // Query the ecs for ocean components that this view will iterate over
    ecs_iter_t ocean_it = ecs_query_iter(ecs, sys->ocean_query);
    while (ecs_query_next(&ocean_it)) {
      const uint32_t ocean_count = ocean_it.count;
      if (ocean_count == 0) {
        continue;
      }
      const TbOceanComponent *oceans =
          ecs_field(&ocean_it, TbOceanComponent, 1);

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
          VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
              sys->tmp_alloc, ocean_count, VkDescriptorSetLayout);
          for (uint32_t i = 0; i < ocean_count; ++i) {
            layouts[i] = sys->set_layout;
          }
          err = tb_rnd_frame_desc_pool_tick(rnd_sys, "ocean", &pool_info,
                                            layouts, NULL, sys->ocean_pools,
                                            ocean_count, ocean_count);
          TB_VK_CHECK(err, "Failed to tick ocean's descriptor pool");
        }

        // Just upload and write all views for now, they tend to be important
        // anyway
        const uint32_t write_count = ocean_count * 3;
        tb_auto writes =
            tb_alloc_nm_tp(sys->tmp_alloc, write_count, VkWriteDescriptorSet);
        tb_auto buffer_info =
            tb_alloc_nm_tp(sys->tmp_alloc, ocean_count, VkDescriptorBufferInfo);
        tb_auto depth_info =
            tb_alloc_nm_tp(sys->tmp_alloc, ocean_count, VkDescriptorImageInfo);
        tb_auto color_info =
            tb_alloc_nm_tp(sys->tmp_alloc, ocean_count, VkDescriptorImageInfo);

        for (uint32_t oc_idx = 0; oc_idx < ocean_count; ++oc_idx) {
          const TbOceanComponent *ocean_comp = &oceans[oc_idx];

          const uint32_t write_idx = oc_idx * 3;

          const uint32_t wave_count =
              SDL_max(ocean_comp->wave_count, TB_WAVE_MAX);

          OceanData data = {
              .time_waves = tb_f4(time, wave_count, 0, 0),
          };
          SDL_memcpy(data.wave, ocean_comp->waves,
                     wave_count * sizeof(TbOceanWave));

          // Write ocean data into the tmp buffer we know will wind up on the
          // GPU
          uint64_t offset = 0;
          err = tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, sizeof(OceanData), 0x40,
                                              &data, &offset);
          TB_VK_CHECK(err,
                      "Failed to make tmp host buffer allocation for ocean");

          VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys);

          // Get the descriptor we want to write to
          VkDescriptorSet ocean_set =
              tb_rnd_frame_desc_pool_get_set(rnd_sys, sys->ocean_pools, oc_idx);

          buffer_info[oc_idx] = (VkDescriptorBufferInfo){
              .buffer = tmp_gpu_buffer,
              .offset = offset,
              .range = sizeof(OceanData),
          };

          VkImageView depth_view = tb_render_target_get_view(
              sys->rt_sys, rnd_sys->frame_idx, sys->rt_sys->depth_buffer_copy);

          VkImageView color_view = tb_render_target_get_view(
              sys->rt_sys, rnd_sys->frame_idx, sys->rt_sys->color_copy);

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
        tb_rnd_update_descriptors(rnd_sys, write_count, writes);
      }

      // Draw the ocean
      {
        OceanPushConstants ocean_consts = {
            .m = tb_transform_to_matrix(&sys->ocean_transform)};

        // Max camera * ocean * tile draw batches are required
        uint32_t batch_count = 0;
        const uint32_t batch_max = ocean_count * tile_count;

        tb_auto ocean_batches =
            tb_alloc_nm_tp(sys->tmp_alloc, batch_max, OceanDrawBatch);

        tb_auto ocean_draw_batches =
            tb_alloc_nm_tp(sys->tmp_alloc, batch_max, TbDrawBatch);
        tb_auto prepass_draw_batches =
            tb_alloc_nm_tp(sys->tmp_alloc, batch_max, TbDrawBatch);

        for (uint32_t ocean_idx = 0; ocean_idx < ocean_count; ++ocean_idx) {
          tb_auto ocean_set = tb_rnd_frame_desc_pool_get_set(
              sys->rnd_sys, sys->ocean_pools, ocean_idx);

          ocean_draw_batches[batch_count] = (TbDrawBatch){
              .pipeline = tb_shader_get_pipeline(ecs, sys->ocean_pass_shader),
              .layout = sys->pipe_layout,
              .viewport = {0, height, width, -(float)height, 0, 1},
              .scissor = {{0, 0}, {width, height}},
              .user_batch = &ocean_batches[batch_count],
          };
          prepass_draw_batches[batch_count] = (TbDrawBatch){
              .pipeline =
                  tb_shader_get_pipeline(ecs, sys->ocean_prepass_shader),
              .layout = sys->pipe_layout,
              .viewport = {0, height, width, -(float)height, 0, 1},
              .scissor = {{0, 0}, {width, height}},
              .user_batch = &ocean_batches[batch_count],
          };
          ocean_batches[batch_count] = (OceanDrawBatch){
              .view_set = view_set,
              .ocean_set = ocean_set,
              .consts = ocean_consts,
              .inst_buffer = tb_rnd_get_gpu_tmp_buffer(sys->rnd_sys),
              .inst_offset = tile_offset,
              .inst_count = visible_tile_count,
              .geom_buffer =
                  tb_mesh_sys_get_gpu_mesh(ecs, sys->ocean_patch_mesh2),
              .index_type = (VkIndexType)sys->ocean_index_type,
              .index_count = sys->ocean_index_count,
              .pos_offset = sys->ocean_pos_offset,
          };
          batch_count++;
        }

        // Draw to the prepass and the ocean pass
        tb_render_pipeline_issue_draw_batch(sys->rp_sys,
                                            sys->trans_depth_draw_ctx,
                                            batch_count, prepass_draw_batches);
        tb_render_pipeline_issue_draw_batch(sys->rp_sys,
                                            sys->trans_color_draw_ctx,
                                            batch_count, ocean_draw_batches);
      }
    }
  }

  TracyCZoneEnd(ctx);
}

void ocean_on_start(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Ocean On Start Sys", true);
  tb_auto ecs = it->world;

  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 1);
  tb_auto rp_sys = ecs_field(it, TbRenderPipelineSystem, 2);
  tb_auto mesh_sys = ecs_field(it, TbMeshSystem, 3);
  tb_auto view_sys = ecs_field(it, TbViewSystem, 4);
  tb_auto rt_sys = ecs_field(it, TbRenderTargetSystem, 5);
  tb_auto vlog = ecs_field(it, TbVisualLoggingSystem, 6);
  tb_auto aud_sys = ecs_field(it, TbAudioSystem, 7);

  tb_auto world = ecs_singleton_get(ecs, TbWorldRef)->world;
  tb_auto ocean_sys = ecs_singleton_get_mut(ecs, TbOceanSystem);

  init_ocean_system(ecs, ocean_sys, world->gp_alloc, world->tmp_alloc, rnd_sys,
                    rp_sys, mesh_sys, view_sys, rt_sys, vlog, aud_sys);

  ecs_singleton_modified(ecs, TbOceanSystem);

  TracyCZoneEnd(ctx);
}

void tb_register_ocean_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Ocean Sys", true);
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbOceanSystem);

  // Query must be initialized outside of ecs progress
  TbOceanSystem sys = {
      .ocean_query = ecs_query(ecs, {.filter.terms =
                                         {
                                             {.id = ecs_id(TbOceanComponent)},
                                         }}),
  };
  ecs_singleton_set_ptr(ecs, TbOceanSystem, &sys);

  // ocean_on_start must be no_readonly because it enqueues a mesh load request
  ecs_system(
      ecs,
      {.entity = ecs_entity(
           ecs, {.name = "ocean_on_start", .add = {ecs_dependson(EcsOnStart)}}),
       .query.filter.terms =
           {
               {.id = ecs_id(TbRenderSystem), .src.id = ecs_id(TbRenderSystem)},
               {.id = ecs_id(TbRenderPipelineSystem),
                .src.id = ecs_id(TbRenderPipelineSystem)},
               {.id = ecs_id(TbMeshSystem), .src.id = ecs_id(TbMeshSystem)},
               {.id = ecs_id(TbViewSystem), .src.id = ecs_id(TbViewSystem)},
               {.id = ecs_id(TbRenderTargetSystem),
                .src.id = ecs_id(TbRenderTargetSystem)},
               {.id = ecs_id(TbVisualLoggingSystem),
                .src.id = ecs_id(TbVisualLoggingSystem)},
               {.id = ecs_id(TbAudioSystem), .src.id = ecs_id(TbAudioSystem)},
           },
       .callback = ocean_on_start,
       .no_readonly = true});

  ECS_SYSTEM(ecs, ocean_audio_tick, EcsOnUpdate, TbOceanSystem(TbOceanSystem),
             TbOceanComponent);
  ECS_SYSTEM(ecs, ocean_draw_tick, EcsOnStore, TbOceanSystem(TbOceanSystem),
             TbCameraComponent);
  TracyCZoneEnd(ctx);
}

void tb_unregister_ocean_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  TbOceanSystem *sys = ecs_singleton_get_mut(ecs, TbOceanSystem);
  ecs_query_fini(sys->ocean_query);
  destroy_ocean_system(sys);

  tb_shader_destroy(ecs, sys->ocean_pass_shader);
  tb_shader_destroy(ecs, sys->ocean_prepass_shader);

  ecs_singleton_remove(ecs, TbOceanSystem);
}
