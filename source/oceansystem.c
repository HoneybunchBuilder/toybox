#include "oceansystem.h"

#include "assets.h"
#include "audiosystem.h"
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
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"
#include "world.h"

#include <flecs.h>

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "ocean_frag.h"
#include "ocean_vert.h"
#include "oceanprepass_frag.h"
#include "oceanprepass_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

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

void ocean_record(VkCommandBuffer buffer, uint32_t batch_count,
                  const DrawBatch *batches) {
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const OceanDrawBatch *ocean_batch =
        (const OceanDrawBatch *)batch->user_batch;
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
                          uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Ocean Prepass Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean Prepass", 2, true);
  cmd_begin_label(buffer, "Ocean Prepass", f4(0.0f, 0.4f, 0.4f, 1.0f));

  ocean_record(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void ocean_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Ocean Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Ocean", 2, true);
  cmd_begin_label(buffer, "Ocean", f4(0.0f, 0.8f, 0.8f, 1.0f));

  ocean_record(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_ocean_pipelines(RenderSystem *render_system,
                                VkFormat color_format, VkFormat depth_format,
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
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Ocean Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean pipeline", err);

  create_info.pNext = &(VkPipelineRenderingCreateInfo){
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .depthAttachmentFormat = depth_format,
  };
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
#ifdef TB_USE_INVERSE_DEPTH
          .depthCompareOp = VK_COMPARE_OP_GREATER,
#else
          .depthCompareOp = VK_COMPARE_OP_LESS,
#endif
          .maxDepthBounds = 1.0f,
      },

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

OceanSystem create_ocean_system(
    Allocator std_alloc, Allocator tmp_alloc, RenderSystem *render_system,
    RenderPipelineSystem *render_pipe_system, MeshSystem *mesh_system,
    ViewSystem *view_system, RenderTargetSystem *render_target_system,
    VisualLoggingSystem *vlog, AudioSystem *audio_system) {
  OceanSystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .render_system = render_system,
      .render_pipe_system = render_pipe_system,
      .mesh_system = mesh_system,
      .view_system = view_system,
      .render_target_system = render_target_system,
      .vlog = vlog,
      .audio_system = audio_system,

  };

  // Load sound effects
  {
    char file_name[100] = {0};
    for (uint32_t i = 0; i < 4; ++i) {
      SDL_snprintf(file_name, 100, "audio/wave0%d.wav", i + 1);
      char *wave_path = tb_resolve_asset_path(sys.tmp_alloc, file_name);
      sys.wave_sounds[i] =
          tb_audio_system_load_effect(sys.audio_system, wave_path);
    }
    // And music
    /*
    char *mus_path = tb_resolve_asset_path(sys.tmp_alloc, "audio/test.ogg");
    sys.music = tb_audio_system_load_music(sys.audio_system, mus_path);
    tb_audio_play_music(sys.audio_system, sys.music);
    */
  }

  // Load the known glb that has the ocean mesh
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(sys.tmp_alloc, "scenes/Ocean.glb");

  // Load glb off disk
  cgltf_data *data = tb_read_glb(sys.std_alloc, asset_path);
  TB_CHECK(data, "Failed to load glb");

  // Parse expected mesh from glb
  {
    cgltf_mesh *ocean_mesh = &data->meshes[0];
    // Must put mesh name on std_alloc for proper cleanup
    {
      const char *static_name = "Ocean";
      char *name = tb_alloc_nm_tp(sys.std_alloc, sizeof(static_name) + 1, char);
      SDL_snprintf(name, sizeof(static_name), "%s", static_name);
      ocean_mesh->name = name;
    }

    sys.ocean_transform = tb_transform_from_node(&data->nodes[0]);

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

      AABB local_aabb = aabb_init();
      aabb_add_point(&local_aabb, f3(min[0], min[1], min[2]));
      aabb_add_point(&local_aabb, f3(max[0], max[1], max[2]));

      float4x4 m = transform_to_matrix(&sys.ocean_transform);
      local_aabb = aabb_transform(m, local_aabb);

      sys.tile_width = aabb_get_width(local_aabb);
      sys.tile_depth = aabb_get_depth(local_aabb);
    }

    sys.ocean_index_type = ocean_mesh->primitives->indices->stride == 2
                               ? VK_INDEX_TYPE_UINT16
                               : VK_INDEX_TYPE_UINT32;
    sys.ocean_index_count = ocean_mesh->primitives->indices->count;

    uint64_t index_size =
        sys.ocean_index_count *
        (sys.ocean_index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    uint64_t idx_padding = index_size % (sizeof(uint16_t) * 4);
    sys.ocean_pos_offset = index_size + idx_padding;

    sys.ocean_patch_mesh =
        tb_mesh_system_load_mesh(mesh_system, asset_path, &data->nodes[0]);
  }

  cgltf_free(data);

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
    err = tb_rnd_create_sampler(render_system, &create_info, "Ocean Sampler",
                                &sys.sampler);
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
    err = tb_rnd_create_sampler(render_system, &create_info,
                                "Ocean Shadow Sampler", &sys.shadow_sampler);
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
                .pImmutableSamplers = &sys.sampler,
            },
            {
                .binding = 4,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = &sys.shadow_sampler,
            },
        }};
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "Ocean Descriptor Set Layout",
                                   &sys.set_layout);
    TB_VK_CHECK(err, "Failed to create ocean descriptor set layout");
  }

  // Create ocean pipeline layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts =
            (VkDescriptorSetLayout[2]){
                sys.set_layout,
                sys.view_system->set_layout,
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
        render_system, &create_info, "Ocean Pipeline Layout", &sys.pipe_layout);
    TB_VK_CHECK(err, "Failed to create ocean pipeline layout");
  }

  // Retrieve passes
  TbRenderPassId depth_id = sys.render_pipe_system->transparent_depth_pass;
  TbRenderPassId color_id = sys.render_pipe_system->transparent_color_pass;

  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(
        sys.render_pipe_system, sys.render_pipe_system->transparent_depth_pass,
        &attach_count, NULL);
    TB_CHECK(attach_count == 1, "Unexpected");
    PassAttachment depth_info = {0};
    tb_render_pipeline_get_attachments(
        sys.render_pipe_system, sys.render_pipe_system->transparent_depth_pass,
        &attach_count, &depth_info);

    VkFormat depth_format = tb_render_target_get_format(
        sys.render_pipe_system->render_target_system, depth_info.attachment);

    VkFormat color_format = VK_FORMAT_UNDEFINED;
    tb_render_pipeline_get_attachments(
        sys.render_pipe_system, sys.render_pipe_system->transparent_color_pass,
        &attach_count, NULL);
    TB_CHECK(attach_count == 2, "Unexpected");
    PassAttachment attach_info[2] = {0};
    tb_render_pipeline_get_attachments(
        sys.render_pipe_system, sys.render_pipe_system->transparent_color_pass,
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

    err = create_ocean_pipelines(render_system, color_format, depth_format,
                                 sys.pipe_layout, &sys.prepass_pipeline,
                                 &sys.pipeline);
    TB_VK_CHECK(err, "Failed to create ocean pipeline");
  }

  sys.trans_depth_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(OceanDrawBatch),
                              .draw_fn = ocean_prepass_record,
                              .pass_id = depth_id,
                          });
  sys.trans_color_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(OceanDrawBatch),
                              .draw_fn = ocean_pass_record,
                              .pass_id = color_id,
                          });

  sys.ocean_geom_buffer =
      tb_mesh_system_get_gpu_mesh(mesh_system, sys.ocean_patch_mesh);
  TB_CHECK(sys.ocean_geom_buffer, "Failed to get gpu buffer for mesh");
  return sys;
}

void destroy_ocean_system(OceanSystem *self) {
  for (uint32_t i = 0; i < 4; ++i) {
    tb_audio_system_release_effect_ref(self->audio_system,
                                       self->wave_sounds[i]);
  }
  // tb_audio_system_release_music_ref(self->audio_system, self->music);
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->ocean_patch_mesh);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(self->render_system,
                                   self->ocean_pools[i].set_pool);
  }

  tb_rnd_destroy_sampler(self->render_system, self->sampler);
  tb_rnd_destroy_sampler(self->render_system, self->shadow_sampler);

  tb_rnd_destroy_pipeline(self->render_system, self->prepass_pipeline);
  tb_rnd_destroy_pipeline(self->render_system, self->pipeline);

  tb_rnd_destroy_pipe_layout(self->render_system, self->pipe_layout);
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  *self = (OceanSystem){0};
}

void ocean_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Ocean Update System", TracyCategoryColorCore, true);
  OceanComponent *oceans = ecs_field(it, OceanComponent, 1);
  // Update time on all ocean components
  for (int32_t i = 0; i < it->count; ++i) {
    OceanComponent *ocean = &oceans[i];
    ocean->time += it->delta_time;
  }
  TracyCZoneEnd(ctx);
}

void ocean_audio_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Ocean Audio System", TracyCategoryColorAudio, true);

  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, OceanSystem);

  OceanSystem *sys = ecs_singleton_get_mut(ecs, OceanSystem);

  OceanComponent *components = ecs_field(it, OceanComponent, 1);
  if (it->count > 0) {
    (void)components;
    sys->wave_sound_timer -= it->delta_time;
    if (sys->wave_sound_timer <= 0.0f) {
      sys->wave_sound_timer = tb_randf(1.3f, 2.0f);

      uint32_t idx = rand() % TB_OCEAN_SFX_COUNT;
      tb_audio_play_effect(sys->audio_system, sys->wave_sounds[idx]);
    }
  }
  TracyCZoneEnd(ctx);
}

void ocean_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Ocean Draw System", TracyCategoryColorRendering, true);
  ecs_world_t *ecs = it->world;

  ECS_COMPONENT(ecs, OceanSystem);
  ECS_COMPONENT(ecs, OceanComponent);
  OceanSystem *sys = ecs_singleton_get_mut(ecs, OceanSystem);
  ecs_singleton_modified(ecs, OceanSystem);

  // TODO: Make this less hacky
  const uint32_t width = sys->render_system->render_thread->swapchain.width;
  const uint32_t height = sys->render_system->render_thread->swapchain.height;

  CameraComponent *cameras = ecs_field(it, CameraComponent, 1);
  for (int32_t i = 0; i < it->count; ++i) {
    CameraComponent *camera = &cameras[i];

    VkResult err = VK_SUCCESS;
    RenderSystem *render_system = sys->render_system;

    // We want to draw a number of ocean tiles to cover the entire ocean plane
    // Since only visible ocean tiles need to be drawn we can calculate the
    // tiles relative to the view

    // Get the camera's view so we can examine its frustum and decide where to
    // place ocean tiles
    const View *view = tb_get_view(sys->view_system, camera->view_id);
    float4x4 inv_v = inv_mf44(view->view_data.v);

    // Get frustum AABB in view space by taking a unit frustum and
    // transforming it by the view's projection
    AABB frust_aabb = aabb_init();
    {
      float3 frustum_corners[TB_FRUSTUM_CORNER_COUNT] = {{0}};
      for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; ++i) {
        float3 corner = tb_frustum_corners[i];
        // Transform from screen space to world space
        float4 inv_corner = mulf44(view->view_data.inv_vp,
                                   f4(corner[0], corner[1], corner[2], 1.0f));
        frustum_corners[i] = f4tof3(inv_corner) / inv_corner[3];
        frustum_corners[i][TB_HEIGHT_IDX] = 0.0f; // Flatten the AABB
        aabb_add_point(&frust_aabb, frustum_corners[i]);
      }
    }

    // Determine how many tiles we'll need
    uint32_t tile_count = 0;
    uint32_t horiz_tile_count = 0;
    uint32_t deep_tile_count = 0;
    {
      float frust_width = aabb_get_width(frust_aabb);
      float frust_depth = aabb_get_depth(frust_aabb);

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

      float3 view_to_world_offset = f4tof3(inv_v.col3);
      view_to_world_offset[TB_HEIGHT_IDX] = 0.0f;

      for (uint32_t d = 0; d < deep_tile_count; ++d) {
        for (uint32_t h = 0; h < horiz_tile_count; ++h) {
          AABB world_aabb = aabb_init();
          float3 min = f3(-half_width, 0, -half_depth) + pos.xyz;
          float3 max = f3(half_width, 0, half_depth) + pos.xyz;

          aabb_add_point(&world_aabb, min + view_to_world_offset);
          aabb_add_point(&world_aabb, max + view_to_world_offset);

          // TODO: Make frustum test more reliable
          // if (frustum_test_aabb(&view->frustum, &world_aabb))
          {
            float3 offset = pos.xyz;
            offset[TB_HEIGHT_IDX] = 0.0f;
            // tb_vlog_location(self->vlog, offset, 20.0f, f3(0, 1, 0));
            visible_tile_offsets[visible_tile_count++] = f3tof4(offset, 0.0f);
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
      err = tb_rnd_sys_tmp_buffer_copy(render_system, size, 0x40,
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
      const OceanComponent *oceans = ecs_field(&ocean_it, OceanComponent, 1);

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
          err = tb_rnd_frame_desc_pool_tick(render_system, &pool_info, layouts,
                                            sys->ocean_pools, ocean_count);
          TB_VK_CHECK(err, "Failed to tick ocean's descriptor pool");
        }

        // Just upload and write all views for now, they tend to be important
        // anyway
        const uint32_t write_count = ocean_count * 3;
        VkWriteDescriptorSet *writes =
            tb_alloc_nm_tp(sys->tmp_alloc, write_count, VkWriteDescriptorSet);
        VkDescriptorBufferInfo *buffer_info =
            tb_alloc_nm_tp(sys->tmp_alloc, ocean_count, VkDescriptorBufferInfo);
        VkDescriptorImageInfo *depth_info =
            tb_alloc_nm_tp(sys->tmp_alloc, ocean_count, VkDescriptorImageInfo);
        VkDescriptorImageInfo *color_info =
            tb_alloc_nm_tp(sys->tmp_alloc, ocean_count, VkDescriptorImageInfo);

        for (uint32_t oc_idx = 0; oc_idx < ocean_count; ++oc_idx) {
          const OceanComponent *ocean_comp = &oceans[oc_idx];

          const uint32_t write_idx = oc_idx * 3;

          const uint32_t wave_count =
              SDL_max(ocean_comp->wave_count, TB_WAVE_MAX);

          OceanData data = {
              .time_waves = f4(ocean_comp->time, wave_count, 0, 0),
          };
          SDL_memcpy(data.wave, ocean_comp->waves,
                     wave_count * sizeof(OceanWave));

          // Write ocean data into the tmp buffer we know will wind up on the
          // GPU
          uint64_t offset = 0;
          err = tb_rnd_sys_tmp_buffer_copy(render_system, sizeof(OceanData),
                                           0x40, &data, &offset);
          TB_VK_CHECK(err,
                      "Failed to make tmp host buffer allocation for ocean");

          VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);

          // Get the descriptor we want to write to
          VkDescriptorSet ocean_set = tb_rnd_frame_desc_pool_get_set(
              render_system, sys->ocean_pools, oc_idx);

          buffer_info[oc_idx] = (VkDescriptorBufferInfo){
              .buffer = tmp_gpu_buffer,
              .offset = offset,
              .range = sizeof(OceanData),
          };

          VkImageView depth_view = tb_render_target_get_view(
              sys->render_target_system, render_system->frame_idx,
              sys->render_target_system->depth_buffer_copy);

          VkImageView color_view = tb_render_target_get_view(
              sys->render_target_system, render_system->frame_idx,
              sys->render_target_system->color_copy);

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
        tb_rnd_update_descriptors(render_system, write_count, writes);
      }

      // Draw the ocean
      {
        OceanPushConstants ocean_consts = {
            .m = transform_to_matrix(&sys->ocean_transform)};

        // Max camera * ocean * tile draw batches are required
        uint32_t batch_count = 0;
        const uint32_t batch_max = ocean_count * tile_count;

        OceanDrawBatch *ocean_batches =
            tb_alloc_nm_tp(sys->tmp_alloc, batch_max, OceanDrawBatch);

        DrawBatch *ocean_draw_batches =
            tb_alloc_nm_tp(sys->tmp_alloc, batch_max, DrawBatch);
        DrawBatch *prepass_draw_batches =
            tb_alloc_nm_tp(sys->tmp_alloc, batch_max, DrawBatch);

        VkDescriptorSet view_set =
            tb_view_system_get_descriptor(sys->view_system, camera->view_id);

        for (uint32_t ocean_idx = 0; ocean_idx < ocean_count; ++ocean_idx) {
          VkDescriptorSet ocean_set = tb_rnd_frame_desc_pool_get_set(
              sys->render_system, sys->ocean_pools, ocean_idx);

          ocean_draw_batches[batch_count] = (DrawBatch){
              .pipeline = sys->pipeline,
              .layout = sys->pipe_layout,
              .viewport = {0, height, width, -(float)height, 0, 1},
              .scissor = {{0, 0}, {width, height}},
              .user_batch = &ocean_batches[batch_count],
          };
          prepass_draw_batches[batch_count] = (DrawBatch){
              .pipeline = sys->prepass_pipeline,
              .layout = sys->pipe_layout,
              .viewport = {0, height, width, -(float)height, 0, 1},
              .scissor = {{0, 0}, {width, height}},
              .user_batch = &ocean_batches[batch_count],
          };
          ocean_batches[batch_count] = (OceanDrawBatch){
              .view_set = view_set,
              .ocean_set = ocean_set,
              .consts = ocean_consts,
              .inst_buffer = tb_rnd_get_gpu_tmp_buffer(sys->render_system),
              .inst_offset = tile_offset,
              .inst_count = visible_tile_count,
              .geom_buffer = sys->ocean_geom_buffer,
              .index_type = (VkIndexType)sys->ocean_index_type,
              .index_count = sys->ocean_index_count,
              .pos_offset = sys->ocean_pos_offset,
          };
          batch_count++;
        }

        // Draw to the prepass and the ocean pass
        tb_render_pipeline_issue_draw_batch(sys->render_pipe_system,
                                            sys->trans_depth_draw_ctx,
                                            batch_count, prepass_draw_batches);
        tb_render_pipeline_issue_draw_batch(sys->render_pipe_system,
                                            sys->trans_color_draw_ctx,
                                            batch_count, ocean_draw_batches);
      }
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_register_ocean_sys(ecs_world_t *ecs, Allocator std_alloc,
                           Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  ECS_COMPONENT(ecs, MeshSystem);
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, RenderTargetSystem);
  ECS_COMPONENT(ecs, VisualLoggingSystem);
  ECS_COMPONENT(ecs, AudioSystem);
  ECS_COMPONENT(ecs, OceanSystem);
  ECS_COMPONENT(ecs, OceanComponent);
  ECS_COMPONENT(ecs, CameraComponent);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, RenderPipelineSystem);
  MeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, MeshSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  RenderTargetSystem *rt_sys = ecs_singleton_get_mut(ecs, RenderTargetSystem);
  VisualLoggingSystem *vlog = ecs_singleton_get_mut(ecs, VisualLoggingSystem);
  AudioSystem *aud_sys = ecs_singleton_get_mut(ecs, AudioSystem);

  OceanSystem sys =
      create_ocean_system(std_alloc, tmp_alloc, rnd_sys, rp_sys, mesh_sys,
                          view_sys, rt_sys, vlog, aud_sys);

  // Create ocean query for the draw
  sys.ocean_query = ecs_query(ecs, {.filter.terms = {
                                        {.id = ecs_id(OceanComponent)},
                                    }});

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(OceanSystem), OceanSystem, &sys);

  ECS_SYSTEM(ecs, ocean_update_tick, EcsOnUpdate, OceanComponent);
  ECS_SYSTEM(ecs, ocean_audio_tick, EcsOnUpdate, OceanComponent);
  ECS_SYSTEM(ecs, ocean_draw_tick, EcsOnUpdate, CameraComponent);

  tb_register_ocean_component(ecs);
}

void tb_unregister_ocean_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, OceanSystem);
  OceanSystem *sys = ecs_singleton_get_mut(ecs, OceanSystem);
  ecs_query_fini(sys->ocean_query);
  destroy_ocean_system(sys);
  ecs_singleton_remove(ecs, OceanSystem);
}
