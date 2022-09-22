#include "oceansystem.h"

#include "assets.h"
#include "cameracomponent.h"
#include "cgltf.h"
#include "meshsystem.h"
#include "ocean.hlsli"
#include "oceancomponent.h"
#include "profiling.h"
#include "rendersystem.h"
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
#ifdef __clang__
#pragma clang diagnostic pop
#endif

typedef struct OceanDrawBatch {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkViewport viewport;
  VkRect2D scissor;
  OceanPushConstants consts;
  VkDescriptorSet view_set;
  VkDescriptorSet ocean_set;
  VkBuffer geom_buffer;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t pos_offset;
  uint64_t uv_offset;
} OceanDrawBatch;

void ocean_pass_record(VkCommandBuffer buffer, uint32_t batch_count,
                       const void *batches) {
  TracyCZoneNC(ctx, "Mesh Opaque Record", TracyCategoryColorRendering, true);

  const OceanDrawBatch *ocean_batches = (const OceanDrawBatch *)batches;
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const OceanDrawBatch *batch = &ocean_batches[batch_idx];
    VkPipelineLayout layout = batch->layout;
    VkBuffer geom_buffer = batch->geom_buffer;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                            1, &batch->view_set, 0, NULL);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            1, &batch->ocean_set, 0, NULL);
    vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(OceanPushConstants), &batch->consts);

    vkCmdBindIndexBuffer(buffer, geom_buffer, 0, batch->index_type);
    vkCmdBindVertexBuffers(buffer, 0, 1, &geom_buffer, &batch->pos_offset);
    vkCmdBindVertexBuffers(buffer, 1, 1, &geom_buffer, &batch->uv_offset);

    vkCmdDrawIndexed(buffer, batch->index_count, 1, 0, 0, 0);
  }
  TracyCZoneEnd(ctx);
}

VkResult create_ocean_pipeline(RenderSystem *render_system, VkRenderPass pass,
                               VkPipelineLayout pipe_layout,
                               VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule ocean_vert_mod = VK_NULL_HANDLE;
  VkShaderModule ocean_frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };

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

  const uint32_t stage_count = 2;

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = stage_count,
      .pStages =
          (VkPipelineShaderStageCreateInfo[stage_count]){
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
                      {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
                      {1, sizeof(float2), VK_VERTEX_INPUT_RATE_VERTEX},
                  },
              .vertexAttributeDescriptionCount = 2,
              .pVertexAttributeDescriptions =
                  (VkVertexInputAttributeDescription[2]){
                      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                      {1, 1, VK_FORMAT_R32G32_SFLOAT, 0},
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
                      .blendEnable = VK_TRUE,
                      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                      .dstColorBlendFactor =
                          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                      .colorBlendOp = VK_BLEND_OP_ADD,
                      .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                      .dstAlphaBlendFactor =
                          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
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
              .depthWriteEnable = VK_TRUE,
              .depthCompareOp = VK_COMPARE_OP_GREATER,
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

  tb_rnd_destroy_shader(render_system, ocean_vert_mod);
  tb_rnd_destroy_shader(render_system, ocean_frag_mod);

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

  *self = (OceanSystem){
      .render_system = render_system,
      .mesh_system = mesh_system,
      .view_system = view_system,
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
    const cgltf_mesh *ocean_mesh = &data->meshes[0];

    self->ocean_index_type = ocean_mesh->primitives->indices->stride == 2
                                 ? VK_INDEX_TYPE_UINT16
                                 : VK_INDEX_TYPE_UINT32;
    self->ocean_index_count = ocean_mesh->primitives->indices->count;

    uint32_t vertex_count = ocean_mesh->primitives->attributes->data->count;

    uint64_t index_size =
        self->ocean_index_count *
        (self->ocean_index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    uint64_t idx_padding = index_size % (sizeof(float) * 3);
    self->ocean_pos_offset = index_size + idx_padding;
    self->ocean_uv_offset =
        self->ocean_pos_offset +
        (vertex_count * (sizeof(float) * 6) + sizeof(float4));

    self->ocean_patch_mesh =
        tb_mesh_system_load_mesh(mesh_system, asset_path, ocean_mesh);
  }

  VkResult err = VK_SUCCESS;

  // Create ocean descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings =
            &(VkDescriptorSetLayoutBinding){
                .binding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stageFlags =
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
    };
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "Ocean Descriptor Set Layout",
                                   &self->set_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean descriptor set layout", false);
  }

  // Create ocean pipeline layout
  {
    const uint32_t set_layout_count = 2;
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = set_layout_count,
        .pSetLayouts =
            (VkDescriptorSetLayout[set_layout_count]){
                self->set_layout,
                self->view_system->set_layout,
            },
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            &(VkPushConstantRange){
                .size = sizeof(OceanPushConstants),
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Ocean Pipeline Layout",
                                        &self->pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean pipeline layout", false);
  }

  // Create render pass for the ocean
  {
    const uint32_t attachment_count = 2;
    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachment_count,
        .pAttachments =
            (VkAttachmentDescription[attachment_count]){
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
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout =
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .finalLayout =
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                },
            },
        .subpassCount = 1,
        .pSubpasses =
            &(VkSubpassDescription){
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments =
                    &(VkAttachmentReference){
                        0,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    },
                .pDepthStencilAttachment =
                    &(VkAttachmentReference){
                        1,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    },
            },
        .pDependencies =
            &(VkSubpassDependency){
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            },
    };
    err = tb_rnd_create_render_pass(render_system, &create_info, "Ocean Pass",
                                    &self->ocean_pass);
    TB_VK_CHECK_RET(err, "Failed to create ocean pass", false);
  }

  err = create_ocean_pipeline(render_system, self->ocean_pass,
                              self->pipe_layout, &self->pipeline);
  TB_VK_CHECK_RET(err, "Failed to create ocean pipeline", false);

  // Create framebuffers for ocean pass
  {
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
          .renderPass = self->ocean_pass,
          .attachmentCount = attachment_count,
          .pAttachments = attachments,
          .width = render_system->render_thread->swapchain.width,
          .height = render_system->render_thread->swapchain.height,
          .layers = 1,
      };
      err = tb_rnd_create_framebuffer(render_system, &create_info,
                                      "Ocean Pass Framebuffer",
                                      &self->framebuffers[i]);
    }
  }

  // Register the render pass
  tb_rnd_register_pass(render_system, self->ocean_pass, self->framebuffers,
                       render_system->render_thread->swapchain.width,
                       render_system->render_thread->swapchain.height,
                       ocean_pass_record);

  self->ocean_geom_buffer =
      tb_mesh_system_get_gpu_mesh(mesh_system, self->ocean_patch_mesh);
  TB_CHECK_RETURN(self->ocean_geom_buffer, "Failed to get gpu buffer for mesh",
                  false);

  return true;
}

void destroy_ocean_system(OceanSystem *self) {
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->ocean_patch_mesh);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_framebuffer(self->render_system, self->framebuffers[i]);
    OceanSystemFrameState *state = &self->frame_states[i];
    tb_rnd_destroy_descriptor_pool(self->render_system, state->set_pool);
  }

  tb_rnd_destroy_pipeline(self->render_system, self->pipeline);

  tb_rnd_destroy_render_pass(self->render_system, self->ocean_pass);
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

  if (ocean_count == 0 || camera_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Allocate all the descriptor sets for this frame
  {
    VkResult err = VK_SUCCESS;

    RenderSystem *render_system = self->render_system;
    OceanSystemFrameState *state =
        &self->frame_states[render_system->frame_idx];
    VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);
    // Allocate all the descriptor sets for this frame
    {
      // Resize the pool
      if (state->set_count < ocean_count) {
        if (state->set_pool) {
          tb_rnd_destroy_descriptor_pool(render_system, state->set_pool);
        }

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = ocean_count,
            .poolSizeCount = 1,
            .pPoolSizes =
                &(VkDescriptorPoolSize){
                    .descriptorCount = ocean_count,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                },
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        };
        err = tb_rnd_create_descriptor_pool(
            render_system, &create_info,
            "View System Frame State Descriptor Pool", &state->set_pool);
        TB_VK_CHECK(err,
                    "Failed to create view system frame state descriptor pool");
      } else {
        vkResetDescriptorPool(self->render_system->render_thread->device,
                              state->set_pool, 0);
      }
      state->set_count = ocean_count;
      state->sets = tb_realloc_nm_tp(self->std_alloc, state->sets,
                                     state->set_count, VkDescriptorSet);

      VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
          self->tmp_alloc, state->set_count, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < state->set_count; ++i) {
        layouts[i] = self->set_layout;
      }

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = state->set_count,
          .descriptorPool = state->set_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(render_system->render_thread->device,
                                     &alloc_info, state->sets);
      TB_VK_CHECK(err, "Failed to re-allocate view descriptor sets");
    }

    // Just upload and write all views for now, they tend to be important anyway
    VkWriteDescriptorSet *writes =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, VkWriteDescriptorSet);
    VkDescriptorBufferInfo *buffer_info =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, VkDescriptorBufferInfo);
    TbHostBuffer *buffers =
        tb_alloc_nm_tp(self->tmp_alloc, ocean_count, TbHostBuffer);
    for (uint32_t oc_idx = 0; oc_idx < ocean_count; ++oc_idx) {
      const OceanComponent *ocean_comp =
          tb_get_component(oceans, oc_idx, OceanComponent);
      TbHostBuffer *buffer = &buffers[oc_idx];

      OceanData data = {
          .wave_count = ocean_comp->wave_count,
      };
      SDL_memcpy(data.wave, ocean_comp->waves,
                 data.wave_count * sizeof(OceanWave));

      // Write ocean data into the tmp buffer we know will wind up on the GPU
      err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, sizeof(OceanData),
                                             0x40, buffer);
      TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for ocean");

      // Copy view data to the allocated buffer
      SDL_memcpy(buffer->ptr, &data, sizeof(OceanData));

      // Get the descriptor we want to write to
      VkDescriptorSet ocean_set = state->sets[oc_idx];

      buffer_info[oc_idx] = (VkDescriptorBufferInfo){
          .buffer = tmp_gpu_buffer,
          .offset = buffer->offset,
          .range = sizeof(OceanData),
      };

      // Construct a write descriptor
      writes[oc_idx] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = ocean_set,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo = &buffer_info[oc_idx],
      };
    }
    vkUpdateDescriptorSets(self->render_system->render_thread->device,
                           ocean_count, writes, 0, NULL);
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

  // TODO: Make this less hacky
  const uint32_t width = self->render_system->render_thread->swapchain.width;
  const uint32_t height = self->render_system->render_thread->swapchain.height;

  // Max camera * ocean draw batches are required
  uint32_t batch_count = 0;
  const uint32_t batch_max = ocean_count * camera_count;
  OceanDrawBatch *batches =
      tb_alloc_nm_tp(self->tmp_alloc, batch_max, OceanDrawBatch);

  for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
    const CameraComponent *camera =
        tb_get_component(cameras, cam_idx, CameraComponent);

    VkDescriptorSet view_set =
        tb_view_system_get_descriptor(self->view_system, camera->view_id);

    for (uint32_t ocean_idx = 0; ocean_idx < ocean_count; ++ocean_idx) {
      const OceanComponent *ocean_comp =
          tb_get_component(oceans, ocean_idx, OceanComponent);

      VkDescriptorSet ocean_set =
          self->frame_states[self->render_system->frame_idx].sets[ocean_idx];

      // TODO: only if ocean is visible to camera
      batches[batch_count++] = (OceanDrawBatch){
          .pipeline = self->pipeline,
          .layout = self->pipe_layout,
          .viewport = {0, 0, width, height, 0, 1},
          .scissor = {{0, 0}, {width, height}},
          .view_set = view_set,
          .ocean_set = ocean_set,
          .consts = (OceanPushConstants){ocean_comp->time},
          .geom_buffer = self->ocean_geom_buffer,
          .index_type = (VkIndexType)self->ocean_index_type,
          .index_count = self->ocean_index_count,
          .pos_offset = self->ocean_pos_offset,
          .uv_offset = self->ocean_uv_offset,
      };
    }
  }

  tb_rnd_issue_draw_batch(self->render_system, self->ocean_pass, batch_count,
                          sizeof(OceanDrawBatch), batches);

  // Report output (we've updated the time on the ocean component)
  output->set_count = 1;
  output->write_sets[0] = (SystemWriteSet){
      .id = OceanComponentId,
      .count = ocean_count,
      .components = (uint8_t *)out_oceans,
      .entities = ocean_entities,
  };

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
      .dep_count = 2,
      .deps[0] = {2, {OceanComponentId, TransformComponentId}},
      .deps[1] = {2, {CameraComponentId, TransformComponentId}},
      .system_dep_count = 3,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = MeshSystemId,
      .system_deps[2] = ViewSystemId,
      .create = tb_create_ocean_system,
      .destroy = tb_destroy_ocean_system,
      .tick = tb_tick_ocean_system,
  };
}
