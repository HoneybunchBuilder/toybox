#include "visualloggingsystem.h"

#include "assets.h"
#include "cameracomponent.h"
#include "coreuisystem.h"
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

#include <flecs.h>

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

#define TB_MAX_VLOG_DRAWS 1024

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

void vlog_draw_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const DrawBatch *batches) {
  (void)gpu_ctx;
  TracyCZoneNC(ctx, "Visual Logger Record", TracyCategoryColorRendering, true);
  // TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Visual Logger", 3, true);

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const VLogDrawBatch *vlog_batch = (const VLogDrawBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }
    TracyCZoneNC(batch_ctx, "VLog Batch", TracyCategoryColorRendering, true);
    // TracyCVkNamedZone(gpu_ctx, batch_scope, buffer, "VLog Batch", 4, true);
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
            .color = f3tof4(shape->location.color, 1),
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
    // TracyCVkZoneEnd(batch_scope);
    TracyCZoneEnd(batch_ctx);
  }

  // TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_primitive_pipeline(RenderSystem *render_system,
                                   VkFormat color_format, VkFormat depth_format,
                                   VkPipelineLayout layout,
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
#ifdef TB_USE_INVERSE_DEPTH
              .depthCompareOp = VK_COMPARE_OP_GREATER,
#else
              .depthCompareOp = VK_COMPARE_OP_LESS,
#endif
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR},
          },
      .layout = layout,
  };

  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Primitive Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create primitive pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

VisualLoggingSystem create_visual_logging_system(
    Allocator std_alloc, Allocator tmp_alloc, RenderSystem *render_system,
    ViewSystem *view_system, RenderPipelineSystem *render_pipe_system,
    MeshSystem *mesh_system, CoreUISystem *coreui) {
  VisualLoggingSystem sys = {
      .tmp_alloc = tmp_alloc,
      .std_alloc = std_alloc,
      .render_system = render_system,
      .view_system = view_system,
      .render_pipe_system = render_pipe_system,
      .mesh_system = mesh_system,
      .ui = tb_coreui_register_menu(coreui, "Visual Logger"),
  };

  TB_DYN_ARR_RESET(sys.frames, std_alloc, 128);

  // Load some default meshes, load some simple shader pipelines
  VkResult err = VK_SUCCESS;

  {
    // Load the known glb that has the sphere mesh
    // Get qualified path to scene asset
    char *asset_path = tb_resolve_asset_path(tmp_alloc, "scenes/Sphere.glb");

    // Load glb off disk
    cgltf_data *data = tb_read_glb(std_alloc, asset_path);
    TB_CHECK(data, "Failed to load glb");

    // Parse expected mesh from glbs
    {
      cgltf_mesh *sphere_mesh = &data->meshes[0];
      // Must put mesh name on std_alloc for proper cleanup
      {
        const char *static_name = "Sphere";
        char *name = tb_alloc_nm_tp(std_alloc, sizeof(static_name) + 1, char);
        SDL_snprintf(name, sizeof(static_name), "%s", static_name);
        sphere_mesh->name = name;
      }
      sys.sphere_index_type = sphere_mesh->primitives->indices->stride == 2
                                  ? VK_INDEX_TYPE_UINT16
                                  : VK_INDEX_TYPE_UINT32;
      sys.sphere_index_count = sphere_mesh->primitives->indices->count;

      uint64_t index_size =
          sys.sphere_index_count *
          (sys.sphere_index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
      uint64_t idx_padding = index_size % (sizeof(uint16_t) * 4);
      sys.sphere_pos_offset = index_size + idx_padding;

      const cgltf_node *node = &data->nodes[0];
      sys.sphere_mesh = tb_mesh_system_load_mesh(mesh_system, asset_path, node);
      sys.sphere_scale =
          (float3){node->scale[0], node->scale[1], node->scale[2]};
    }

    sys.sphere_geom_buffer =
        tb_mesh_system_get_gpu_mesh(mesh_system, sys.sphere_mesh);
    TB_CHECK(sys.sphere_geom_buffer, "Failed to get gpu buffer for mesh");

    cgltf_free(data);
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
                                        &sys.pipe_layout);
    TB_VK_CHECK(err, "Failed to create primitive pipeline layout");
  }

  {
    uint32_t attach_count = 0;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    tb_render_pipeline_get_attachments(
        render_pipe_system, render_pipe_system->transparent_color_pass,
        &attach_count, NULL);
    TB_CHECK(attach_count == 2, "Unexpected");
    PassAttachment attach_info[2] = {0};
    tb_render_pipeline_get_attachments(
        render_pipe_system, render_pipe_system->transparent_color_pass,
        &attach_count, attach_info);

    for (uint32_t attach_idx = 0; attach_idx < attach_count; ++attach_idx) {
      VkFormat format =
          tb_render_target_get_format(render_pipe_system->render_target_system,
                                      attach_info[attach_idx].attachment);
      if (format == VK_FORMAT_D32_SFLOAT) {
        depth_format = format;
      } else {
        color_format = format;
      }
    }

    err = create_primitive_pipeline(render_system, color_format, depth_format,
                                    sys.pipe_layout, &sys.pipeline);
    TB_VK_CHECK(err, "Failed to create primitive pipeline");
  }

  {
    DrawContextDescriptor desc = {
        .batch_size = sizeof(VLogDrawBatch),
        .draw_fn = vlog_draw_record,
        .pass_id = render_pipe_system->transparent_color_pass,
    };
    sys.draw_ctx =
        tb_render_pipeline_register_draw_context(render_pipe_system, &desc);
  }

  return sys;
}

void destroy_visual_logging_system(VisualLoggingSystem *self) {
#ifndef FINAL
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->sphere_mesh);

  tb_rnd_destroy_pipe_layout(self->render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(self->render_system, self->pipeline);

  TB_DYN_ARR_DESTROY(self->frames);
#endif
  *self = (VisualLoggingSystem){0};
}

void vlog_draw_tick(ecs_iter_t *it) {
  (void)it;
#ifndef FINAL
  TracyCZoneNC(ctx, "Visual Logging System Draw", TracyCategoryColorCore, true);
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Visual Logging System Draw");

  VisualLoggingSystem *sys = ecs_field(it, VisualLoggingSystem, 1);
  const CameraComponent *cameras = ecs_field(it, CameraComponent, 2);

  // Render primitives from selected frame
  if (sys->logging && sys->frames.capacity > 0) {
    const VLogFrame *frame = &TB_DYN_ARR_AT(sys->frames, sys->log_frame_idx);

    // TODO: Make this less hacky
    const uint32_t width = sys->render_system->render_thread->swapchain.width;
    const uint32_t height = sys->render_system->render_thread->swapchain.height;

    // Get the vp matrix for the primary view
    for (int32_t i = 0; i < it->count; ++i) {
      const CameraComponent *camera = &cameras[i];

      for (uint32_t i = 0; i < frame->line_draw_count; ++i) {
        const VLogLine *line = &frame->line_draws[i];
        (void)line;
        // TODO: Encode line draws into the line batch
      }

      VLogDrawBatch *loc_batch = tb_alloc_tp(sys->tmp_alloc, VLogDrawBatch);
      *loc_batch = (VLogDrawBatch){
          .index_count = sys->sphere_index_count,
          .pos_offset = sys->sphere_pos_offset,
          .shape_geom_buffer = sys->sphere_geom_buffer,
          .shape_scale = sys->sphere_scale,
          .type = TB_VLOG_SHAPE_LOCATION,
          .view_set =
              tb_view_system_get_descriptor(sys->view_system, camera->view_id),
      };

      DrawBatch *batch = tb_alloc_tp(sys->tmp_alloc, DrawBatch);
      *batch = (DrawBatch){
          .layout = sys->pipe_layout,
          .pipeline = sys->pipeline,
          .viewport = (VkViewport){0, height, width, -(float)height, 0, 1},
          .scissor = (VkRect2D){{0, 0}, {width, height}},
          .user_batch = loc_batch,
          .draw_count = frame->loc_draw_count,
          .draw_size = sizeof(VLogShape),
          .draws =
              tb_alloc_nm_tp(sys->tmp_alloc, frame->loc_draw_count, VLogShape),
      };
      for (uint32_t i = 0; i < frame->loc_draw_count; ++i) {
        ((VLogShape *)batch->draws)[i].location = frame->loc_draws[i];
      }

      tb_render_pipeline_issue_draw_batch(sys->render_pipe_system,
                                          sys->draw_ctx, 1, batch);
    }
  }

  TracyCZoneEnd(ctx);
#endif
}

void vlog_ui_tick(ecs_iter_t *it) {
  (void)it;
#ifndef FINAL
  TracyCZoneNC(ctx, "Visual Logging System UI", TracyCategoryColorCore, true);
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Visual Logging System UI");

  VisualLoggingSystem *sys = ecs_field(it, VisualLoggingSystem, 1);

  uint32_t frame_count = TB_DYN_ARR_SIZE(sys->frames);
  uint32_t frame_cap = sys->frames.capacity;

  // UI for recording visual logs
  if (sys->ui && *sys->ui) {
    if (igBegin("Visual Logger", sys->ui, 0)) {
      igText("Recording: %s", sys->recording ? "true" : "false");

      if (sys->recording) {
        if (igButton("Stop", (ImVec2){0})) {
          sys->recording = false;
        }
      } else {
        if (igButton("Start", (ImVec2){0})) {
          sys->recording = true;
        }
      }

      if (sys->recording) {
        igText("Recording Frame %d", frame_count);
      } else {
        igText("Recorded %d Frames", frame_count);
      }
      igText("%d frames allocated", frame_cap);

      igSeparator();

      if (igButton(sys->logging ? "Stop Rendering" : "Start Rendering",
                   (ImVec2){0})) {
        sys->logging = !sys->logging;
      }

      static bool render_latest = true;
      igCheckbox("Render Latest Frame", &render_latest);
      if (render_latest) {
        sys->log_frame_idx = frame_count;
      } else {

        igText("Selected Frame:");
        igSliderInt("##frame", &sys->log_frame_idx, 0, frame_cap, "%d", 0);
      }

      igEnd();
    }
  }

  if (sys->recording) {
    // If we're recording make sure that we're properly keeping the frame
    // collection large enough so that the next frame can issue draws
    if (frame_count + 1 >= frame_cap) {
      // TODO: Resizing a collection like this can cause serious stutter when
      // the collection gets large enough. We should probably instead allocate
      // from a free-list of buckets so that we're never re-sizing a large
      // collection
      TB_DYN_ARR_RESERVE(sys->frames, frame_cap + 1024);
    }
    // If recording, insert a new frame
    TB_DYN_ARR_APPEND(sys->frames, (VLogFrame){0});
  }

  TracyCZoneEnd(ctx);
#endif
}

void tb_register_visual_logging_sys(ecs_world_t *ecs, Allocator std_alloc,
                                    Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  ECS_COMPONENT(ecs, MeshSystem);
  ECS_COMPONENT(ecs, CoreUISystem);
  ECS_COMPONENT(ecs, CameraComponent);
  ECS_COMPONENT(ecs, VisualLoggingSystem);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  ViewSystem *view_sys = ecs_singleton_get_mut(ecs, ViewSystem);
  RenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, RenderPipelineSystem);
  MeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, MeshSystem);
  CoreUISystem *coreui = ecs_singleton_get_mut(ecs, CoreUISystem);

  VisualLoggingSystem sys = create_visual_logging_system(
      std_alloc, tmp_alloc, rnd_sys, view_sys, rp_sys, mesh_sys, coreui);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(VisualLoggingSystem), VisualLoggingSystem, &sys);

  ECS_SYSTEM(ecs, vlog_draw_tick, EcsOnUpdate,
             VisualLoggingSystem(VisualLoggingSystem), CameraComponent);
  ECS_SYSTEM(ecs, vlog_ui_tick, EcsOnUpdate,
             VisualLoggingSystem(VisualLoggingSystem));
}

void tb_unregister_visual_logging_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, VisualLoggingSystem);
  VisualLoggingSystem *sys = ecs_singleton_get_mut(ecs, VisualLoggingSystem);
  destroy_visual_logging_system(sys);
  ecs_singleton_remove(ecs, VisualLoggingSystem);
}

VLogFrame *get_current_frame(VisualLoggingSystem *vlog) {
  // Expecting that upon creation the vlogger system has at least some space
  // allocated and that upon ticking the system will ensure that there will
  // always be a frame to put draws into.
  uint32_t tail = TB_DYN_ARR_SIZE(vlog->frames) - 1;
  VLogFrame *frame = &TB_DYN_ARR_AT(vlog->frames, tail);
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
  TB_DYN_ARR_CLEAR(vlog->frames);
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
