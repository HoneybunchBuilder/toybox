#include "visualloggingsystem.h"

#include "assets.h"
#include "cameracomponent.h"
#include "meshsystem.h"
#include "profiling.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "tbimgui.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "primitive_frag.h"
#include "primitive_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

typedef struct VLogLocation {
  float3 position;
  float radius;
  float3 color;
} VLogLocation;

typedef struct VLogLine {
  float3 start;
  float3 end;
  float3 color;
} VLogLine;

typedef union VLogShape {
  VLogLocation location;
  VLogLine line;
} VLogShape;

typedef enum VLogShapeType {
  TB_VLOG_SHAPE_LOCATION,
  TB_VLOG_SHAPE_LINE,
} VLogShapeType;

#define TB_MAX_VLOG_DRAWS 50

typedef struct VLogFrame {
  float timestamp;
  uint32_t loc_draw_count;
  VLogLocation loc_draws[TB_MAX_VLOG_DRAWS];
  uint32_t line_draw_count;
  VLogLine line_draws[TB_MAX_VLOG_DRAWS];
} VLogFrame;

typedef struct VLogDrawBatch {
  VkDescriptorSet view_set;
  VkBuffer shape_geom_buffer;
  float3 shape_scale;
  uint32_t index_count;
  uint64_t pos_offset;
  VLogShapeType type;
} VLogDrawBatch;

TB_DEFINE_SYSTEM(visual_logging, VisualLoggingSystem,
                 VisualLoggingSystemDescriptor)

void vlog_draw_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "Visual Logger Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Visual Logger", 1, true);

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const VLogDrawBatch *vlog_batch = (const VLogDrawBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }
    TracyCZoneNC(batch_ctx, "VLog Batch", TracyCategoryColorRendering, true);
    TracyCVkNamedZone(gpu_ctx, batch_scope, buffer, "VLog Batch", 2, true);
    cmd_begin_label(buffer, "VLog Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      const VLogShape *shape = &((const VLogShape *)batch->draws)[draw_idx];

      // Set push constants for draw
      if (vlog_batch->type == TB_VLOG_SHAPE_LOCATION) {
        const float3 scale = vlog_batch->shape_scale * shape->location.radius;
        PrimitivePushConstants consts = {
            .position = shape->location.position,
            .scale = scale,
            .color = shape->location.color,
        };
        vkCmdPushConstants(buffer, batch->layout, VK_SHADER_STAGE_ALL_GRAPHICS,
                           0, sizeof(PrimitivePushConstants), &consts);
      } else if (vlog_batch->type == TB_VLOG_SHAPE_LINE) {
        // TODO: Set push constants for line drawing
      }

      vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
      vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              batch->layout, 0, 1, &vlog_batch->view_set, 0,
                              NULL);

      vkCmdBindIndexBuffer(buffer, vlog_batch->shape_geom_buffer, 0,
                           VK_INDEX_TYPE_UINT16);
      vkCmdBindVertexBuffers(buffer, 0, 1, &vlog_batch->shape_geom_buffer,
                             &vlog_batch->pos_offset);

      // Note: This is probably better suited to instanced drawing
      // Maybe a good first pass at instancing in the future
      vkCmdDrawIndexed(buffer, vlog_batch->index_count, 1, 0, 0, 0);
    }

    cmd_end_label(buffer);
    TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }

  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void tb_visual_logging_system_descriptor(
    SystemDescriptor *desc, const VisualLoggingSystemDescriptor *vlog_desc) {
  *desc = (SystemDescriptor){
      .name = "VisualLogger",
      .size = sizeof(VisualLoggingSystem),
      .id = VisualLoggingSystemId,
      .desc = (InternalDescriptor)vlog_desc,
      .dep_count = 1,
      .deps[0] = {.count = 2,
                  .dependent_ids = {CameraComponentId, TransformComponentId}},
      .system_dep_count = 5,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = ViewSystemId,
      .system_deps[2] = RenderObjectSystemId,
      .system_deps[3] = RenderPipelineSystemId,
      .system_deps[4] = MeshSystemId,
      .create = tb_create_visual_logging_system,
      .destroy = tb_destroy_visual_logging_system,
      .tick = tb_tick_visual_logging_system,
  };
}

VLogFrame *get_current_frame(VisualLoggingSystem *vlog) {
  // Expecting that upon creation the vlogger system has at least some space
  // allocated and that upon ticking the system will ensure that there will
  // always be a frame to put draws into.
  TB_CHECK_RETURN(vlog->frames, "Invalid frame collection", NULL);
  TB_CHECK_RETURN(vlog->frame_count < vlog->frame_max, "Frame out of range",
                  NULL);

  VLogFrame *frame = &vlog->frames[vlog->frame_count];
  TB_CHECK(frame, "Invalid frame");
  return frame;
}

VLogLocation *frame_acquire_location(VLogFrame *frame) {
  TB_CHECK_RETURN(frame->loc_draw_count < TB_MAX_VLOG_DRAWS,
                  "Draw count exceeded frame max", NULL);
  return &frame->loc_draws[frame->loc_draw_count++];
}

VLogLine *frame_acquire_line(VLogFrame *frame) {
  TB_CHECK_RETURN(frame->line_draw_count < TB_MAX_VLOG_DRAWS,
                  "Draw count exceeded frame max", NULL);
  return &frame->line_draws[frame->line_draw_count++];
}

VLogShape *vlog_acquire_frame_shape(VisualLoggingSystem *vlog,
                                    VLogShapeType type) {
  VLogFrame *frame = get_current_frame(vlog);
  if (type == TB_VLOG_SHAPE_LOCATION) {
    return (VLogShape *)frame_acquire_location(frame);
  } else if (type == TB_VLOG_SHAPE_LINE) {
    return (VLogShape *)frame_acquire_line(frame);
  } else {
    TB_CHECK_RETURN(false, "Invalid shape", NULL);
  }
}

VkResult create_primitive_pipeline(RenderSystem *render_system,
                                   VkRenderPass pass, VkPipelineLayout layout,
                                   VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(primitive_vert);
    create_info.pCode = (const uint32_t *)primitive_vert;
    err = tb_rnd_create_shader(render_system, &create_info, "Primitive Vert",
                               &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load primitive vert shader module", err);

    create_info.codeSize = sizeof(primitive_frag);
    create_info.pCode = (const uint32_t *)primitive_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Primitive Frag",
                               &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load primitive frag shader module", err);
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
              }},
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
              .attachmentCount = 1,
              .pAttachments =
                  &(VkPipelineColorBlendAttachmentState){
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
                                         "Primitive Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create primitive pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

bool create_visual_logging_system(VisualLoggingSystem *self,
                                  const VisualLoggingSystemDescriptor *desc,
                                  uint32_t system_dep_count,
                                  System *const *system_deps) {
#ifndef FINAL
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which visual logger depend on",
                  false);
  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  TB_CHECK_RETURN(view_system,
                  "Failed to find view system which visual logger depend on",
                  false);
  RenderPipelineSystem *render_pipe_system =
      tb_get_system(system_deps, system_dep_count, RenderPipelineSystem);
  TB_CHECK_RETURN(
      render_pipe_system,
      "Failed to find render pipeline system which visual logger depend on",
      false);
  MeshSystem *mesh_system =
      tb_get_system(system_deps, system_dep_count, MeshSystem);
  TB_CHECK_RETURN(mesh_system,
                  "Failed to find mesh system which visual logger depend on",
                  false);

  *self = (VisualLoggingSystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .render_system = render_system,
      .view_system = view_system,
      .render_pipe_system = render_pipe_system,
      .mesh_system = mesh_system,
  };

  // Load some default meshes, load some simple shader pipelines
  VkResult err = VK_SUCCESS;

  {
    // Load the known glb that has the ocean mesh
    // Get qualified path to scene asset
    char *asset_path =
        tb_resolve_asset_path(self->tmp_alloc, "scenes/Sphere.glb");

    // Load glb off disk
    cgltf_data *data = tb_read_glb(self->std_alloc, asset_path);
    TB_CHECK_RETURN(data, "Failed to load glb", false);

    // Parse expected mesh from glbs
    {
      cgltf_mesh *sphere_mesh = &data->meshes[0];
      sphere_mesh->name = "Sphere";
      self->sphere_index_type = sphere_mesh->primitives->indices->stride == 2
                                    ? VK_INDEX_TYPE_UINT16
                                    : VK_INDEX_TYPE_UINT32;
      self->sphere_index_count = sphere_mesh->primitives->indices->count;

      uint64_t index_size =
          self->sphere_index_count *
          (self->sphere_index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
      uint64_t idx_padding = index_size % (sizeof(uint16_t) * 4);
      self->sphere_pos_offset = index_size + idx_padding;

      const cgltf_node *node = &data->nodes[0];
      self->sphere_mesh =
          tb_mesh_system_load_mesh(mesh_system, asset_path, node);

      TB_CHECK(node->has_scale, "Unexpected");
      self->sphere_scale =
          (float3){node->scale[0], node->scale[1], node->scale[2]};
    }

    self->sphere_geom_buffer =
        tb_mesh_system_get_gpu_mesh(mesh_system, self->sphere_mesh);
    TB_CHECK_RETURN(self->sphere_geom_buffer,
                    "Failed to get gpu buffer for mesh", false);
  }

  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (VkPushConstantRange[1]){
                {
                    .size = sizeof(PrimitivePushConstants),
                    .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                },
            },
        .setLayoutCount = 1,
        .pSetLayouts =
            (VkDescriptorSetLayout[1]){
                view_system->set_layout,
            },
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Primitive Pipeline Layout",
                                        &self->pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create primitive pipeline layout", false);
  }

  {
    VkRenderPass pass = tb_render_pipeline_get_pass(
        render_pipe_system, render_pipe_system->transparent_color_pass);
    err = create_primitive_pipeline(render_system, pass, self->pipe_layout,
                                    &self->pipeline);
    TB_VK_CHECK_RET(err, "Failed to create primitive pipeline", false);
  }

  {
    DrawContextDescriptor desc = {
        .batch_size = sizeof(VLogDrawBatch),
        .draw_fn = vlog_draw_record,
        .pass_id = render_pipe_system->transparent_color_pass,
    };
    self->draw_ctx =
        tb_render_pipeline_register_draw_context(render_pipe_system, &desc);
  }

#endif
  return true;
}

void destroy_visual_logging_system(VisualLoggingSystem *self) {
#ifndef FINAL
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->sphere_mesh);

  tb_rnd_destroy_pipe_layout(self->render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(self->render_system, self->pipeline);
#endif
  *self = (VisualLoggingSystem){0};
}

void tick_visual_logging_system(VisualLoggingSystem *self,
                                const SystemInput *input, SystemOutput *output,
                                float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;

#ifndef FINAL
  TracyCZoneNC(ctx, "Visual Logging System", TracyCategoryColorCore, true);

  // Render primitives from selected frame
  if (self->logging && self->frame_max > 0) {
    const VLogFrame *frame = &self->frames[self->log_frame_idx];

    // TODO: Make this less hacky
    const uint32_t width = self->render_system->render_thread->swapchain.width;
    const uint32_t height =
        self->render_system->render_thread->swapchain.height;

    // Get the vp matrix for the primary view
    const PackedComponentStore *camera_store =
        tb_get_column_check_id(input, 0, 0, CameraComponentId);
    const CameraComponent *camera =
        tb_get_component(camera_store, 0, CameraComponent);

    for (uint32_t i = 0; i < frame->line_draw_count; ++i) {
      const VLogLine *line = &frame->line_draws[i];
      (void)line;
      // TODO: Encode line draws into the line batch
    }

    VLogDrawBatch *loc_batch = tb_alloc_tp(self->tmp_alloc, VLogDrawBatch);
    *loc_batch = (VLogDrawBatch){
        .index_count = self->sphere_index_count,
        .pos_offset = self->sphere_pos_offset,
        .shape_geom_buffer = self->sphere_geom_buffer,
        .shape_scale = self->sphere_scale,
        .type = TB_VLOG_SHAPE_LOCATION,
        .view_set =
            tb_view_system_get_descriptor(self->view_system, camera->view_id),
    };

    DrawBatch *batch = tb_alloc_tp(self->tmp_alloc, DrawBatch);
    *batch = (DrawBatch){
        .layout = self->pipe_layout,
        .pipeline = self->pipeline,
        .viewport = (VkViewport){0, height, width, -(float)height, 0, 1},
        .scissor = (VkRect2D){{0, 0}, {width, height}},
        .user_batch = loc_batch,
        .draw_count = frame->loc_draw_count,
        .draw_size = sizeof(VLogShape),
        .draws =
            tb_alloc_nm_tp(self->tmp_alloc, frame->loc_draw_count, VLogShape),
    };
    for (uint32_t i = 0; i < frame->loc_draw_count; ++i) {
      ((VLogShape *)batch->draws)[i].location = frame->loc_draws[i];
    }

    tb_render_pipeline_issue_draw_batch(self->render_pipe_system,
                                        self->draw_ctx, 1, batch);
  }

  // UI for recording visual logs
  if (igBegin("Visual Logger", NULL, 0)) {
    igText("Recording: %s", self->recording ? "true" : "false");

    if (self->recording) {
      if (igButton("Stop", (ImVec2){0})) {
        self->recording = false;
      }
    } else {
      if (igButton("Start", (ImVec2){0})) {
        self->recording = true;
      }
    }

    if (self->recording) {
      igText("Recording Frame %d", self->frame_count);
    } else {
      igText("Recorded %d Frames", self->frame_count);
    }
    igText("%d frames allocated", self->frame_max);

    igSeparator();

    igText("Selected Frame:");
    igSliderInt("##frame", &self->log_frame_idx, 0, self->frame_max, "%d", 0);
    if (self->log_frame_idx < 0 || self->frame_max == 0) {
      self->log_frame_idx = 0;
    } else if (self->frame_count > 0 &&
               self->log_frame_idx >= (int32_t)self->frame_count) {
      self->log_frame_idx = (int32_t)self->frame_count - 1;
    }
    if (igButton(self->logging ? "Stop Rendering" : "Start Rendering",
                 (ImVec2){0})) {
      self->logging = !self->logging;
    }

    igEnd();
  }

  if (self->recording) {
    // If we're recording make sure that we're properly keeping the frame
    // collection large enough so that the next frame can issue draws
    uint32_t next_frame_count = self->frame_count + 1;
    if (next_frame_count >= self->frame_max) {
      // Add 128 frames of buffer at a time, this means we're not constantly
      // allocating but we also won't be allocating any extremely large chunks
      // which could result in a stall at later points
      self->frame_max = next_frame_count + 127;
      self->frames = tb_realloc_nm_tp(self->std_alloc, self->frames,
                                      self->frame_max, VLogFrame);
    }
    self->frame_count = next_frame_count;
  }

  TracyCZoneEnd(ctx);
#endif
}

// The public API down here is expected to be elided by the compiler when
// producing a shipping build. This behavior needs to be verified

void tb_vlog_begin_recording(VisualLoggingSystem *vlog) {
  (void)vlog;
#ifndef FINAL
  TB_CHECK(vlog->recording == false, "Visual Logger is already recording");
  vlog->recording = true;
#endif
}
void tb_vlog_end_recording(VisualLoggingSystem *vlog) {
  (void)vlog;
#ifndef FINAL
  TB_CHECK(vlog->recording == true, "Visual Logger is not recording");
  vlog->recording = false;
#endif
}
void tb_vlog_clear(VisualLoggingSystem *vlog) {
  (void)vlog;
#ifndef FINAL
  vlog->frame_count = 0;
#endif
}

void tb_vlog_line(VisualLoggingSystem *vlog, float3 start, float3 end,
                  float3 color) {
  (void)vlog;
  (void)start;
  (void)end;
  (void)color;
#ifndef FINAL
  if (!vlog->recording) {
    return;
  }
  VLogShape *draw = vlog_acquire_frame_shape(vlog, TB_VLOG_SHAPE_LINE);
  *draw = (VLogShape){
      .line =
          {
              .start = start,
              .end = end,
              .color = color,
          },
  };
#endif
}

void tb_vlog_location(VisualLoggingSystem *vlog, float3 position, float radius,
                      float3 color) {
  (void)vlog;
  (void)position;
  (void)radius;
  (void)color;
#ifndef FINAL
  if (!vlog->recording) {
    return;
  }
  VLogShape *draw = vlog_acquire_frame_shape(vlog, TB_VLOG_SHAPE_LOCATION);
  *draw = (VLogShape){
      .location =
          {
              .position = position,
              .radius = radius,
              .color = color,
          },
  };
#endif
}
