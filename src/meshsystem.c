#include "meshsystem.h"

#include "cameracomponent.h"
#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "materialsystem.h"
#include "meshcomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "transformcomponent.h"
#include "world.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
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

VkResult create_mesh_pipelines(RenderSystem *render_system, Allocator tmp_alloc,
                               Allocator std_alloc, VkRenderPass pass,
                               VkPipelineLayout pipe_layout,
                               uint32_t *pipe_count, VkPipeline **pipelines) {
  VkResult err = VK_SUCCESS;

  // VI 1: Position & Normal - P3N3
  // VI 2: Position & Normal & Texcoord0 - P3N3U2
  // VI 3: Position & Normal & Tangent & Texcoord0 - P3N3T4U2

  VkVertexInputBindingDescription vert_bindings_P3N3[2] = {
      {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputBindingDescription vert_bindings_P3N3U2[3] = {
      {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {2, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputBindingDescription vert_bindings_P3N3T4U2[4] = {
      {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {2, sizeof(float) * 4, VK_VERTEX_INPUT_RATE_VERTEX},
      {3, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
  };

  VkVertexInputAttributeDescription vert_attrs_P3N3[2] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
  };

  VkVertexInputAttributeDescription vert_attrs_P3N3U2[3] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {2, 2, VK_FORMAT_R32G32_SFLOAT, 0},
  };

  VkVertexInputAttributeDescription vert_attrs_P3N3T4U2[4] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
      {3, 3, VK_FORMAT_R32G32_SFLOAT, 0},
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
  const uint32_t dyn_state_count = 2;
  VkDynamicState dyn_states[dyn_state_count] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = dyn_state_count,
      .pDynamicStates = dyn_states,
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

void opaque_pass_record(VkCommandBuffer buffer, uint32_t batch_count,
                        const void *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Record", TracyCategoryColorRendering, true);

  const MeshDrawBatch *mesh_batches = (const MeshDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const MeshDrawBatch *batch = &mesh_batches[batch_idx];
    if (batch->view_count == 0) {
      continue;
    }
    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    for (uint32_t view_idx = 0; view_idx < batch->view_count; ++view_idx) {
      const MeshDrawView *view = &batch->views[view_idx];
      if (view->draw_count == 0) {
        continue;
      }
      vkCmdSetViewport(buffer, 0, 1, &view->viewport);
      vkCmdSetScissor(buffer, 0, 1, &view->scissor);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              2, 1, &view->view_set, 0, NULL);
      for (uint32_t draw_idx = 0; draw_idx < view->draw_count; ++draw_idx) {
        const MeshDraw *draw = &view->draws[draw_idx];
        if (draw->submesh_draw_count == 0) {
          continue;
        }
        vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                1, 1, &draw->obj_set, 0, NULL);
        VkBuffer geom_buffer = draw->geom_buffer;

        for (uint32_t sub_idx = 0; sub_idx < draw->submesh_draw_count;
             ++sub_idx) {
          const SubMeshDraw *submesh = &draw->submesh_draws[sub_idx];
          if (submesh->index_count > 0) {
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
          }
        }
      }
    }
  }

  TracyCZoneEnd(ctx);
}

bool create_mesh_system(MeshSystem *self, const MeshSystemDescriptor *desc,
                        uint32_t system_dep_count, System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which meshes depend on", false);
  MaterialSystem *material_system =
      (MaterialSystem *)tb_find_system_dep_self_by_id(
          system_deps, system_dep_count, MaterialSystemId);
  TB_CHECK_RETURN(material_system,
                  "Failed to find material system which meshes depend on",
                  false);

  *self = (MeshSystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .render_system = render_system,
      .material_system = material_system,
  };

  // Setup mesh system for rendering
  {
    VkResult err = VK_SUCCESS;

    // Create render pass for opaque meshes
    {
      const uint32_t attachment_count = 2;
      VkAttachmentDescription attachments[attachment_count] = {
          {
              .format = render_system->render_thread->swapchain.format,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
          {
              .format = VK_FORMAT_D32_SFLOAT,
              .samples = VK_SAMPLE_COUNT_1_BIT,
              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
              .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
              .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          },
      };
      const uint32_t color_ref_count = 1;
      VkAttachmentReference color_refs[color_ref_count] = {
          {
              0,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          },
      };
      VkSubpassDescription subpass = {
          .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
          .colorAttachmentCount = color_ref_count,
          .pColorAttachments = color_refs,
          .pDepthStencilAttachment =
              &(VkAttachmentReference){
                  1,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              },
      };
      VkSubpassDependency subpass_dep = {
          .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      };
      VkRenderPassCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .attachmentCount = attachment_count,
          .pAttachments = attachments,
          .subpassCount = 1,
          .pSubpasses = &subpass,
          .pDependencies = &subpass_dep,
      };
      err = tb_rnd_create_render_pass(render_system, &create_info,
                                      "Opaque Mesh Pass", &self->opaque_pass);
      TB_VK_CHECK_RET(err, "Failed to create opaque mesh render pass", false);
    }

    // Create descriptor set layout for objects
    {
      VkDescriptorSetLayoutBinding bindings[1] = {
          {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
           NULL},
      };
      VkDescriptorSetLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 1,
          .pBindings = bindings,
      };
      err = tb_rnd_create_set_layout(render_system, &create_info,
                                     "Object Descriptor Set",
                                     &self->obj_set_layout);
      TB_VK_CHECK_RET(err, "Failed to create object descriptor set layout",
                      false);
    }

    // Create descriptor set layout for views
    {
      VkDescriptorSetLayoutBinding bindings[1] = {
          {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
           VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
      };
      VkDescriptorSetLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 1,
          .pBindings = bindings,
      };
      err = tb_rnd_create_set_layout(render_system, &create_info,
                                     "View Descriptor Set",
                                     &self->view_set_layout);
      TB_VK_CHECK_RET(err, "Failed to create view descriptor set layout",
                      false);
    }

    // Create pipeline layout
    {
      VkDescriptorSetLayout mat_set_layout =
          tb_mat_system_get_set_layout(material_system);
      VkDescriptorSetLayout obj_set_layout = self->obj_set_layout;
      VkDescriptorSetLayout view_set_layout = self->view_set_layout;
      const uint32_t layout_count = 3;
      VkDescriptorSetLayout layouts[layout_count] = {
          mat_set_layout,
          obj_set_layout,
          view_set_layout,
      };

      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = layout_count,
          .pSetLayouts = layouts,
      };
      err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                          "GLTF Pipeline Layout",
                                          &self->pipe_layout);
    }

    // Create framebuffers that associate opaque pass with swapchain and depth
    // targets

    for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
      const uint32_t attachment_count = 2;
      // TODO: Figure out a way to do this without referencing the render thread
      // directly
      VkImageView attachments[attachment_count] = {
          render_system->render_thread->frame_states[i].swapchain_image_view,
          render_system->render_thread->frame_states[i].depth_buffer_view,
      };

      VkFramebufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .renderPass = self->opaque_pass,
          .attachmentCount = attachment_count,
          .pAttachments = attachments,
          .width = render_system->render_thread->swapchain.width,
          .height = render_system->render_thread->swapchain.height,
          .layers = 1,
      };
      err = tb_rnd_create_framebuffer(render_system, &create_info,
                                      "Opaque Pass Framebuffer",
                                      &self->framebuffers[i]);
    }

    err = create_mesh_pipelines(self->render_system, self->tmp_alloc,
                                self->std_alloc, self->opaque_pass,
                                self->pipe_layout, &self->pipe_count,
                                &self->pipelines);
    TB_VK_CHECK_RET(err, "Failed to create mesh pipelines", false);
  }
  // Register the render pass
  tb_rnd_register_pass(render_system, self->opaque_pass, self->framebuffers,
                       render_system->render_thread->swapchain.width,
                       render_system->render_thread->swapchain.height,
                       opaque_pass_record);

  return true;
}

void destroy_mesh_system(MeshSystem *self) {
  RenderSystem *render_system = self->render_system;

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_framebuffer(render_system, self->framebuffers[i]);
    tb_rnd_destroy_descriptor_pool(render_system,
                                   self->frame_states[i].view_set_pool);
    tb_rnd_destroy_descriptor_pool(render_system,
                                   self->frame_states[i].obj_set_pool);
  }

  for (uint32_t i = 0; i < self->pipe_count; ++i) {
    tb_rnd_destroy_pipeline(render_system, self->pipelines[i]);
  }

  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_set_layout(render_system, self->view_set_layout);
  tb_rnd_destroy_set_layout(render_system, self->obj_set_layout);
  tb_rnd_destroy_render_pass(render_system, self->opaque_pass);

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

MeshSystemFrameState *tick_frame_state(MeshSystem *self, uint32_t view_count,
                                       uint32_t mesh_count) {
  MeshSystemFrameState *state =
      &self->frame_states[self->render_system->frame_idx];

  VkResult err = VK_SUCCESS;
  VkDevice device = self->render_system->render_thread->device;
  const VkAllocationCallbacks *vk_alloc =
      &self->render_system->vk_host_alloc_cb;

  // Allocate descriptor sets
  {
    Allocator alloc = self->std_alloc;

    // One set per camera
    {
      state->view_count = view_count;
      if (view_count > state->view_max) {
        const uint32_t new_max = view_count * 2;

        // Resize the descriptor pool
        {
          if (state->view_set_pool) {
            vkDestroyDescriptorPool(device, state->view_set_pool, vk_alloc);
          }

          VkDescriptorPoolCreateInfo create_info = {
              .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
              .maxSets = new_max,
              .poolSizeCount = 1,
              .pPoolSizes =
                  &(VkDescriptorPoolSize){
                      .descriptorCount = 1,
                      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  },
              .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
          };
          err = vkCreateDescriptorPool(device, &create_info, vk_alloc,
                                       &state->view_set_pool);
          TB_VK_CHECK(err, "Failed to create view descriptor pool");
          SET_VK_NAME(device, state->view_set_pool,
                      VK_OBJECT_TYPE_DESCRIPTOR_POOL, "View Set Pool");
        }

        // Reallocate descriptors
        state->view_sets =
            tb_realloc_nm_tp(alloc, state->view_sets, new_max, VkDescriptorSet);

        state->view_max = new_max;

      } else {
        err = vkResetDescriptorPool(device, state->view_set_pool, 0);
        TB_VK_CHECK(err, "Failed to reset view descriptor pool");
      }

      VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
          self->tmp_alloc, state->view_max, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < state->view_max; ++i) {
        layouts[i] = self->view_set_layout;
      }

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = state->view_max,
          .descriptorPool = state->view_set_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(device, &alloc_info, state->view_sets);
      TB_VK_CHECK(err, "Failed to re-allocate view descriptor sets");
    }

    // One set per mesh
    {
      state->obj_count = mesh_count;
      if (mesh_count > state->obj_max) {
        const uint32_t new_max = mesh_count * 2;

        // Resize the descriptor pool
        {
          if (state->obj_set_pool) {
            vkDestroyDescriptorPool(device, state->obj_set_pool, vk_alloc);
          }

          VkDescriptorPoolCreateInfo create_info = {
              .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
              .maxSets = new_max,
              .poolSizeCount = 1,
              .pPoolSizes =
                  &(VkDescriptorPoolSize){
                      .descriptorCount = 1,
                      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  },
              .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
          };
          err = vkCreateDescriptorPool(device, &create_info, vk_alloc,
                                       &state->obj_set_pool);
          TB_VK_CHECK(err, "Failed to create object descriptor pool");
          SET_VK_NAME(device, state->obj_set_pool,
                      VK_OBJECT_TYPE_DESCRIPTOR_POOL, "Object Set Pool");
        }

        // Reallocate descriptors
        state->obj_sets =
            tb_realloc_nm_tp(alloc, state->obj_sets, new_max, VkDescriptorSet);

        state->obj_max = new_max;

      } else {
        err = vkResetDescriptorPool(device, state->obj_set_pool, 0);
        TB_VK_CHECK(err, "Failed to reset object descriptor pool");
      }

      VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
          self->tmp_alloc, state->obj_max, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < state->obj_max; ++i) {
        layouts[i] = self->obj_set_layout;
      }

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = state->obj_max,
          .descriptorPool = state->obj_set_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(device, &alloc_info, state->obj_sets);
      TB_VK_CHECK(err, "Failed to re-allocate object descriptor sets");
    }
  }

  return state;
}

void tick_mesh_system(MeshSystem *self, const SystemInput *input,
                      SystemOutput *output, float delta_seconds) {
  (void)output;

  (void)self;
  (void)delta_seconds;

  TracyCZoneN(ctx, "Mesh System", true);
  TracyCZoneColor(ctx, TracyCategoryColorRendering);

  const uint32_t camera_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *camera_store =
      tb_get_column_check_id(input, 0, 0, CameraComponentId);
  const PackedComponentStore *camera_transform_store =
      tb_get_column_check_id(input, 0, 1, TransformComponentId);

  const uint32_t mesh_count = tb_get_column_component_count(input, 1);
  const PackedComponentStore *mesh_store =
      tb_get_column_check_id(input, 1, 0, MeshComponentId);
  const PackedComponentStore *mesh_transform_store =
      tb_get_column_check_id(input, 1, 1, TransformComponentId);

  if (mesh_count == 0 || camera_count == 0) {
    return;
  }

  // Just collect a batch for every possible pipeline
  const uint32_t batch_count = max_pipe_count;

  Allocator tmp_alloc = self->render_system->render_thread
                            ->frame_states[self->render_system->frame_idx]
                            .tmp_alloc.alloc;

  // Allocate and initialize each batch
  MeshDrawBatch *batches =
      tb_alloc_nm_tp(tmp_alloc, batch_count, MeshDrawBatch);
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    // Each batch could use each view
    MeshDrawBatch *batch = &batches[batch_idx];
    *batch = (MeshDrawBatch){0};
    batch->views = tb_alloc_nm_tp(tmp_alloc, camera_count, MeshDrawView);

    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      MeshDrawView *view = &batch->views[cam_idx];
      // Each view could see each mesh
      // Each mesh could have TB_SUBMESH_MAX # of submeshes
      *view = (MeshDrawView){0};
      view->draws = tb_alloc_nm_tp(tmp_alloc, mesh_count, MeshDraw);
    }
  }

  VkResult err = VK_SUCCESS;
  VkDevice device = self->render_system->render_thread->device;
  VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(self->render_system);

  MeshSystemFrameState *state =
      tick_frame_state(self, camera_count, mesh_count);

  // TODO: Make this less hacky
  const uint32_t width = self->render_system->render_thread->swapchain.width;
  const uint32_t height = self->render_system->render_thread->swapchain.height;

  for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
    const CameraComponent *camera =
        tb_get_component(camera_store, cam_idx, CameraComponent);
    const TransformComponent *cam_trans =
        tb_get_component(camera_transform_store, cam_idx, TransformComponent);

    // Update camera descriptor sets
    VkDescriptorSet view_set = state->view_sets[cam_idx];
    CommonCameraData camera_data = {.view_pos = {0}};
    {
      // Upload transform to the tmp buffer
      TbHostBuffer host_buffer = {0};
      err = tb_rnd_sys_alloc_tmp_host_buffer(
          self->render_system, sizeof(CommonCameraData), 0x40, &host_buffer);
      TB_VK_CHECK(err, "Failed to allocate space on the tmp host buffer");

      const Transform *camera_transform = &cam_trans->transform;

      float3 view_pos = camera_transform->position;

      // TODO: Instead of calculating the vp matrix here, a camera system could
      // do it earlier in the frame
      float4x4 vp = {.row0 = {0}};
      {
        float4x4 proj = {.row0 = {0}};
        perspective(&proj, camera->fov, camera->aspect_ratio, camera->near,
                    camera->far);

        float4x4 model = {.row0 = {0}};
        transform_to_matrix(&model, camera_transform);
        float3 forward = f4tof3(model.row2);

        float4x4 view = {.row0 = {0}};
        look_forward(&view, view_pos, forward, (float3){0.0f, 1.0f, 0.0f});

        mulmf44(&proj, &view, &vp);
      }
      float4x4 inv_vp = {.row0 = {0}}; // TODO

      camera_data = (CommonCameraData){
          .view_pos = view_pos,
          .inv_vp = inv_vp,
          .vp = vp,
      };
      SDL_memcpy(host_buffer.ptr, &camera_data, sizeof(CommonCameraData));

      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = view_set,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo =
              &(VkDescriptorBufferInfo){
                  .buffer = tmp_gpu_buffer,
                  .offset = host_buffer.offset,
                  .range = sizeof(CommonCameraData),
              },
      };
      vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    }

    for (uint32_t mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
      const MeshComponent *mesh_comp =
          tb_get_component(mesh_store, mesh_idx, MeshComponent);
      const TransformComponent *mesh_trans =
          tb_get_component(mesh_transform_store, mesh_idx, TransformComponent);

      // Update object descriptor sets
      VkDescriptorSet obj_set = state->obj_sets[mesh_idx];
      {
        // Upload transform to the tmp buffer
        TbHostBuffer host_buffer = {0};
        err = tb_rnd_sys_alloc_tmp_host_buffer(
            self->render_system, sizeof(CommonObjectData), 0x40, &host_buffer);
        TB_VK_CHECK(err, "Failed to allocate space on the tmp host buffer");

        CommonObjectData obj_data = {.m = {.row0 = {0}}};
        transform_to_matrix(&obj_data.m, &mesh_trans->transform);
        mulmf44(&camera_data.vp, &obj_data.m, &obj_data.mvp);

        SDL_memcpy(host_buffer.ptr, &obj_data, sizeof(CommonObjectData));

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = obj_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .buffer = tmp_gpu_buffer,
                    .offset = host_buffer.offset,
                    .range = sizeof(CommonObjectData),
                },
        };
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
      }

      // Organize draws into batches
      {
        // Determine which pipeline is in use
        VkBuffer geom_buffer =
            tb_mesh_system_get_gpu_mesh(self, mesh_comp->mesh_id);

        for (uint32_t sub_idx = 0; sub_idx < mesh_comp->submesh_count;
             ++sub_idx) {
          const SubMesh *submesh = &mesh_comp->submeshes[sub_idx];

          TbMaterialPerm mat_perm =
              tb_mat_system_get_perm(self->material_system, submesh->material);
          VkDescriptorSet material_set =
              tb_mat_system_get_set(self->material_system, submesh->material);

          uint32_t pipe_idx = get_pipeline_for_input_and_mat(
              self, submesh->vertex_input, mat_perm);

          MeshDrawBatch *batch = &batches[pipe_idx];
          batch->pipeline = self->pipelines[pipe_idx];
          batch->layout = self->pipe_layout; // Can we avoid putting this here?
          batch->view_count = camera_count;
          MeshDrawView *view = &batch->views[cam_idx];
          view->view_set = view_set;
          view->viewport = (VkViewport){0, 0, width, height, 0, 1};
          view->scissor = (VkRect2D){{0, 0}, {width, height}};
          view->draw_count = mesh_count;
          MeshDraw *draw = &view->draws[mesh_idx];
          draw->geom_buffer = geom_buffer;
          draw->obj_set = obj_set;
          draw->submesh_draw_count = mesh_comp->submesh_count;
          SubMeshDraw *sub_draw = &view->draws[mesh_idx].submesh_draws[sub_idx];
          *sub_draw = (SubMeshDraw){
              .mat_set = material_set,
              .index_type = submesh->index_type,
              .index_count = submesh->index_count,
              .index_offset = submesh->index_offset,
          };

          const uint64_t base_vert_offset = submesh->vertex_offset;
          const uint32_t vertex_count = submesh->vertex_count;

          static const uint64_t float3_size = sizeof(float) * 3;

          switch (submesh->vertex_input) {
          case VI_P3N3:
            sub_draw->vertex_binding_count = 2;
            sub_draw->vertex_binding_offsets[0] = base_vert_offset;
            sub_draw->vertex_binding_offsets[1] =
                base_vert_offset + (vertex_count * float3_size);
            break;
          case VI_P3N3U2:
            sub_draw->vertex_binding_count = 3;
            sub_draw->vertex_binding_offsets[0] = base_vert_offset;
            sub_draw->vertex_binding_offsets[1] =
                base_vert_offset + (vertex_count * float3_size);
            sub_draw->vertex_binding_offsets[2] =
                base_vert_offset + (vertex_count * float3_size * 2);
            break;
          case VI_P3N3T4U2:
            sub_draw->vertex_binding_count = 4;
            sub_draw->vertex_binding_offsets[0] = base_vert_offset;
            sub_draw->vertex_binding_offsets[1] =
                base_vert_offset + (vertex_count * float3_size * 1);
            sub_draw->vertex_binding_offsets[2] =
                base_vert_offset + (vertex_count * float3_size * 2);
            sub_draw->vertex_binding_offsets[3] =
                base_vert_offset +
                (vertex_count * ((float3_size * 2) + sizeof(float4)));
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
  tb_rnd_issue_draw_batch(self->render_system, self->opaque_pass, batch_count,
                          sizeof(MeshDrawBatch), batches);

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
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {CameraComponentId, TransformComponentId},
  };
  desc->deps[1] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {MeshComponentId, TransformComponentId},
  };
  desc->system_dep_count = 2;
  desc->system_deps[0] = RenderSystemId;
  desc->system_deps[1] = MaterialSystemId;
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

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_mesh *mesh) {
  // Hash the mesh's path and gltf name to get the id
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

    // Determine how big this mesh is
    uint64_t geom_size = 0;
    uint64_t vertex_offset = 0;
    uint32_t attrib_count = 0;
    uint64_t vertex_input = 0;
    {
      uint64_t index_size = 0;
      uint64_t vertex_size = 0;
      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];
        cgltf_accessor *indices = prim->indices;

        index_size += indices->buffer_view->size;

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

            if (type == cgltf_attribute_type_position) {
              vertex_input |= VA_INPUT_PERM_POSITION;
            } else if (type == cgltf_attribute_type_normal) {
              vertex_input |= VA_INPUT_PERM_NORMAL;
            } else if (type == cgltf_attribute_type_tangent) {
              vertex_input |= VA_INPUT_PERM_TANGENT;
            } else if (type == cgltf_attribute_type_texcoord) {
              vertex_input |= VA_INPUT_PERM_TEXCOORD0;
            }

            attrib_count++;
          }
        }

        // Calculate the necessary padding between the index and vertex contents
        // of the buffer.
        // Otherwise we'll get a validation error.
        // The vertex content needs to start that the correct attribAddress
        // which must be a multiple of the size of the first attribute
        uint64_t idx_padding = index_size % (sizeof(float) * 3);
        vertex_offset = index_size + idx_padding;
        geom_size = vertex_offset + vertex_size;
      }
    }

    VkResult err = VK_SUCCESS;

    // Allocate space on the host that we can read the mesh into
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = geom_size,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      };

      const uint32_t name_max = 100;
      char name[name_max] = {0};
      SDL_snprintf(name, name_max, "%s Host Geom Buffer", mesh->name);

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

      const uint32_t name_max = 100;
      char name[name_max] = {0};
      SDL_snprintf(name, name_max, "%s GPU Geom Buffer", mesh->name);

      err = tb_rnd_sys_alloc_gpu_buffer(self->render_system, &create_info, name,
                                        &self->mesh_gpu_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to create gpu mesh buffer", false);
    }

    // Read the cgltf mesh into the driver owned memory
    {
      TbHostBuffer *host_buf = &self->mesh_host_buffers[index];
      uint64_t idx_offset = 0;
      uint64_t vtx_offset = vertex_offset;
      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];

        {
          cgltf_accessor *indices = prim->indices;
          cgltf_buffer_view *view = indices->buffer_view;
          cgltf_size src_offset = indices->offset + view->offset;
          cgltf_size index_size = view->size;

          void *src = ((uint8_t *)view->buffer->data) + src_offset;
          void *dst = ((uint8_t *)(host_buf->ptr)) + idx_offset;
          SDL_memcpy(dst, src, index_size);
          idx_offset += index_size;
        }

        // Reorder attributes
        uint32_t *attr_order =
            tb_alloc(self->tmp_alloc, sizeof(uint32_t) * attrib_count);
        for (uint32_t i = 0; i < (uint32_t)prim->attributes_count; ++i) {
          cgltf_attribute_type attr_type = prim->attributes[i].type;
          int32_t attr_idx = prim->attributes[i].index;
          if (attr_type == cgltf_attribute_type_position) {
            attr_order[0] = i;
          } else if (attr_type == cgltf_attribute_type_normal) {
            attr_order[1] = i;
          } else if (attr_type == cgltf_attribute_type_tangent) {
            attr_order[2] = i;
          } else if (attr_type == cgltf_attribute_type_texcoord &&
                     attr_idx == 0) {
            if (vertex_input & VA_INPUT_PERM_TANGENT) {
              attr_order[3] = i;
            } else {
              attr_order[2] = i;
            }
          }
        }

        for (cgltf_size attr_idx = 0; attr_idx < attrib_count; ++attr_idx) {
          cgltf_attribute *attr = &prim->attributes[attr_order[attr_idx]];
          cgltf_accessor *accessor = attr->data;
          cgltf_buffer_view *view = accessor->buffer_view;

          size_t attr_offset = view->offset + accessor->offset;
          size_t attr_size = accessor->stride * accessor->count;

          // TODO: Figure out how to handle when an object can't use the
          // expected pipeline
          if (SDL_strcmp(attr->name, "NORMAL") == 0) {
            if (attr_idx + 1 < prim->attributes_count) {
              cgltf_attribute *next =
                  &prim->attributes[attr_order[attr_idx + 1]];
              if (vertex_input & VA_INPUT_PERM_TANGENT) {
                if (SDL_strcmp(next->name, "TANGENT") != 0) {
                  SDL_TriggerBreakpoint();
                }
              } else {
                if (SDL_strcmp(next->name, "TEXCOORD_0") != 0) {
                  SDL_TriggerBreakpoint();
                }
              }
            }
          }

          void *src = ((uint8_t *)view->buffer->data) + attr_offset;
          void *dst = ((uint8_t *)(host_buf->ptr)) + vtx_offset;
          SDL_memcpy(dst, src, attr_size);
          vtx_offset += attr_size;
        }
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