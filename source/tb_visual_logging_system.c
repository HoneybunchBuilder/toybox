#include "tb_visual_logging_system.h"

#include "tb_assets.h"
#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_coreui_system.h"
#include "tb_gltf.h"
#include "tb_imgui.h"
#include "tb_mesh_rnd_sys.h"
#include "tb_mesh_system.h"
#include "tb_profiling.h"
#include "tb_render_object_system.h"
#include "tb_render_pipeline_system.h"
#include "tb_render_system.h"
#include "tb_shader_system.h"
#include "tb_transform_component.h"
#include "tb_view_system.h"
#include "tb_world.h"

#include <flecs.h>

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "primitive_frag.h"
#include "primitive_vert.h"
#pragma clang diagnostic pop

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

typedef struct TbVLogFrame {
  float timestamp;
  uint32_t loc_draw_count;
  VLogLocation loc_draws[TB_MAX_VLOG_DRAWS];
  uint32_t line_draw_count;
  VLogLine line_draws[TB_MAX_VLOG_DRAWS];
} TbVLogFrame;

typedef struct VLogDrawBatch {
  VkDescriptorSet view_set;
  VkBuffer shape_geom_buffer;
  float3 shape_scale;
  uint32_t index_count;
  uint64_t pos_offset;
  VLogShapeType type;
} VLogDrawBatch;

ECS_COMPONENT_DECLARE(TbVisualLoggingSystem);

void tb_register_visual_logging_sys(TbWorld *world);
void tb_unregister_visual_logging_sys(TbWorld *world);

TB_REGISTER_SYS(tb, visual_logging, TB_VLOG_SYS_PRIO)

void vlog_draw_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const TbDrawBatch *batches) {
  (void)gpu_ctx;
  TracyCZoneNC(ctx, "Visual Logger Record", TracyCategoryColorRendering, true);
  // TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Visual Logger", 3, true);

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
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
        TbPrimitivePushConstants consts = {
            .position = shape->location.position,
            .scale = scale,
            .color = tb_f3tof4(shape->location.color, 1),
        };
        vkCmdPushConstants(buffer, batch->layout, VK_SHADER_STAGE_ALL_GRAPHICS,
                           0, sizeof(TbPrimitivePushConstants), &consts);
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

typedef struct TbPrimPipeArgs {
  TbRenderSystem *rnd_sys;
  VkFormat color_format;
  VkFormat depth_format;
  VkPipelineLayout layout;
} TbPrimPipeArgs;

VkPipeline create_primitive_pipeline(void *args) {
  tb_auto pipe_args = (TbPrimPipeArgs *)args;
  tb_auto rnd_sys = pipe_args->rnd_sys;
  tb_auto color_format = pipe_args->color_format;
  tb_auto depth_format = pipe_args->depth_format;
  tb_auto layout = pipe_args->layout;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(primitive_vert);
    create_info.pCode = (const uint32_t *)primitive_vert;
    tb_rnd_create_shader(rnd_sys, &create_info, "Primitive Vert", &vert_mod);

    create_info.codeSize = sizeof(primitive_frag);
    create_info.pCode = (const uint32_t *)primitive_frag;
    tb_rnd_create_shader(rnd_sys, &create_info, "Primitive Frag", &frag_mod);
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
  VkPipeline pipeline = VK_NULL_HANDLE;
  tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                   "Primitive Pipeline", &pipeline);

  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return pipeline;
}

TbVisualLoggingSystem create_visual_logging_system(
    ecs_world_t *ecs, TbAllocator gp_alloc, TbAllocator tmp_alloc,
    TbRenderSystem *rnd_sys, TbViewSystem *view_sys,
    TbRenderPipelineSystem *rp_sys, TbMeshSystem *mesh_system,
    TbCoreUISystem *coreui) {
  TbVisualLoggingSystem sys = {
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
      .rnd_sys = rnd_sys,
      .view_sys = view_sys,
      .rp_sys = rp_sys,
      .mesh_system = mesh_system,
      .ui = tb_coreui_register_menu(coreui, "Visual Logger"),
  };

  TB_DYN_ARR_RESET(sys.frames, gp_alloc, 128);

  // Load some default meshes, load some simple shader pipelines
  VkResult err = VK_SUCCESS;

  {
    // Load the known glb that has the sphere mesh
    // Get qualified path to scene asset
    char *asset_path = tb_resolve_asset_path(tmp_alloc, "scenes/sphere.glb");

    // Load glb off disk
    cgltf_data *data = tb_read_glb(gp_alloc, asset_path);
    TB_CHECK(data, "Failed to load glb");

    // Parse expected mesh from glbs
    {
      cgltf_mesh *sphere_mesh = &data->meshes[0];
      // Must put mesh name on gp_alloc for proper cleanup
      {
        const char *static_name = "TbSphere";
        char *name = tb_alloc_nm_tp(gp_alloc, sizeof(static_name) + 1, char);
        SDL_snprintf(name, sizeof(static_name) + 1, "%s", static_name);
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
      sys.sphere_mesh2 =
          tb_mesh_sys_load_gltf_mesh(ecs, data, asset_path, "sphere", 0);
      sys.sphere_scale =
          (float3){node->scale[0], node->scale[1], node->scale[2]};
    }

    // cgltf_free(data);
  }

  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (VkPushConstantRange[1]){
                {
                    .size = sizeof(TbPrimitivePushConstants),
                    .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                },
            },
        .setLayoutCount = 1,
        .pSetLayouts =
            (VkDescriptorSetLayout[1]){
                tb_view_sys_get_set_layout(ecs),
            },
    };
    err = tb_rnd_create_pipeline_layout(
        rnd_sys, &create_info, "Primitive Pipeline Layout", &sys.pipe_layout);
    TB_VK_CHECK(err, "Failed to create primitive pipeline layout");
  }

  {
    uint32_t attach_count = 0;
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->transparent_color_pass,
                                       &attach_count, NULL);
    TB_CHECK(attach_count == 2, "Unexpected");
    TbPassAttachment attach_info[2] = {0};
    tb_render_pipeline_get_attachments(rp_sys, rp_sys->transparent_color_pass,
                                       &attach_count, attach_info);

    TbPrimPipeArgs args = {
        .rnd_sys = rnd_sys,
        .layout = sys.pipe_layout,
    };

    for (uint32_t attach_idx = 0; attach_idx < attach_count; ++attach_idx) {
      VkFormat format = tb_render_target_get_format(
          rp_sys->rt_sys, attach_info[attach_idx].attachment);
      if (format == VK_FORMAT_D32_SFLOAT) {
        args.depth_format = format;
      } else {
        args.color_format = format;
      }
    }

    sys.shader = tb_shader_load(ecs, create_primitive_pipeline, &args,
                                sizeof(TbPrimPipeArgs));
  }

  {
    TbDrawContextDescriptor desc = {
        .batch_size = sizeof(VLogDrawBatch),
        .draw_fn = vlog_draw_record,
        .pass_id = rp_sys->transparent_color_pass,
    };
    sys.draw_ctx = tb_render_pipeline_register_draw_context(rp_sys, &desc);
  }

  return sys;
}

void destroy_visual_logging_system(ecs_world_t *ecs,
                                   TbVisualLoggingSystem *self) {
  (void)ecs;
#ifndef FINAL
  tb_rnd_destroy_pipe_layout(self->rnd_sys, self->pipe_layout);
  tb_shader_destroy(ecs, self->shader);

  TB_DYN_ARR_DESTROY(self->frames);
#endif
  *self = (TbVisualLoggingSystem){0};
}

void vlog_draw_tick(ecs_iter_t *it) {
  (void)it;
#ifndef FINAL
  TracyCZoneNC(ctx, "Visual Logging System Draw", TracyCategoryColorCore, true);

  TbVisualLoggingSystem *sys = ecs_field(it, TbVisualLoggingSystem, 1);
  const TbCameraComponent *cameras = ecs_field(it, TbCameraComponent, 2);

  // Require shader to be loaded
  if (!tb_is_shader_ready(it->world, sys->shader)) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Requires meshes to be loaded
  if (!tb_is_mesh_ready(it->world, sys->sphere_mesh2)) {
    TracyCZoneEnd(ctx);
    return;
  }

  // Render primitives from selected frame
  if (sys->logging && sys->frames.capacity > 0) {
    const TbVLogFrame *frame = &TB_DYN_ARR_AT(sys->frames, sys->log_frame_idx);

    // TODO: Make this less hacky
    const uint32_t width = sys->rnd_sys->render_thread->swapchain.width;
    const uint32_t height = sys->rnd_sys->render_thread->swapchain.height;

    // Get the vp matrix for the primary view
    for (int32_t i = 0; i < it->count; ++i) {
      const TbCameraComponent *camera = &cameras[i];

      for (uint32_t i = 0; i < frame->line_draw_count; ++i) {
        const VLogLine *line = &frame->line_draws[i];
        (void)line;
        // TODO: Encode line draws into the line batch
      }

      VLogDrawBatch *loc_batch = tb_alloc_tp(sys->tmp_alloc, VLogDrawBatch);
      *loc_batch = (VLogDrawBatch){
          .index_count = sys->sphere_index_count,
          .pos_offset = sys->sphere_pos_offset,
          .shape_geom_buffer =
              tb_mesh_sys_get_gpu_mesh(it->world, sys->sphere_mesh2),
          .shape_scale = sys->sphere_scale,
          .type = TB_VLOG_SHAPE_LOCATION,
          .view_set =
              tb_view_system_get_descriptor(sys->view_sys, camera->view_id),
      };

      TbDrawBatch *batch = tb_alloc_tp(sys->tmp_alloc, TbDrawBatch);
      *batch = (TbDrawBatch){
          .layout = sys->pipe_layout,
          .pipeline = tb_shader_get_pipeline(it->world, sys->shader),
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

      tb_render_pipeline_issue_draw_batch(sys->rp_sys, sys->draw_ctx, 1, batch);
    }
  }

  TracyCZoneEnd(ctx);
#endif
}

void vlog_ui_tick(ecs_iter_t *it) {
  (void)it;
#ifndef FINAL
  TracyCZoneNC(ctx, "Visual Logging System UI", TracyCategoryColorCore, true);

  TbVisualLoggingSystem *sys = ecs_field(it, TbVisualLoggingSystem, 1);

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
    }
    igEnd();
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
    TB_DYN_ARR_APPEND(sys->frames, (TbVLogFrame){0});
  }

  TracyCZoneEnd(ctx);
#endif
}

void tb_register_visual_logging_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Vlog Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbVisualLoggingSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  tb_auto coreui = ecs_singleton_get_mut(ecs, TbCoreUISystem);

  tb_auto sys =
      create_visual_logging_system(ecs, world->gp_alloc, world->tmp_alloc,
                                   rnd_sys, view_sys, rp_sys, mesh_sys, coreui);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbVisualLoggingSystem), TbVisualLoggingSystem, &sys);

  ECS_SYSTEM(ecs, vlog_draw_tick, EcsPostUpdate,
             TbVisualLoggingSystem(TbVisualLoggingSystem), TbCameraComponent);
  ECS_SYSTEM(ecs, vlog_ui_tick, EcsOnUpdate,
             TbVisualLoggingSystem(TbVisualLoggingSystem));

  TracyCZoneEnd(ctx);
}

void tb_unregister_visual_logging_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  TbVisualLoggingSystem *sys =
      ecs_singleton_get_mut(ecs, TbVisualLoggingSystem);
  destroy_visual_logging_system(ecs, sys);
  ecs_singleton_remove(ecs, TbVisualLoggingSystem);
}

TbVLogFrame *get_current_frame(TbVisualLoggingSystem *vlog) {
  // Expecting that upon creation the vlogger system has at least some space
  // allocated and that upon ticking the system will ensure that there will
  // always be a frame to put draws into.
  uint32_t tail = TB_DYN_ARR_SIZE(vlog->frames) - 1;
  TbVLogFrame *frame = &TB_DYN_ARR_AT(vlog->frames, tail);
  TB_CHECK(frame, "Invalid frame");
  return frame;
}

VLogLocation *frame_acquire_location(TbVLogFrame *frame) {
  TB_CHECK_RETURN(frame->loc_draw_count < TB_MAX_VLOG_DRAWS,
                  "Draw count exceeded frame max", NULL);
  return &frame->loc_draws[frame->loc_draw_count++];
}

VLogLine *frame_acquire_line(TbVLogFrame *frame) {
  TB_CHECK_RETURN(frame->line_draw_count < TB_MAX_VLOG_DRAWS,
                  "Draw count exceeded frame max", NULL);
  return &frame->line_draws[frame->line_draw_count++];
}

VLogShape *vlog_acquire_frame_shape(TbVisualLoggingSystem *vlog,
                                    VLogShapeType type) {
  TbVLogFrame *frame = get_current_frame(vlog);
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

void tb_vlog_begin_recording(TbVisualLoggingSystem *vlog) {
  (void)vlog;
#ifndef FINAL
  TB_CHECK(vlog->recording == false, "Visual Logger is already recording");
  vlog->recording = true;
#endif
}
void tb_vlog_end_recording(TbVisualLoggingSystem *vlog) {
  (void)vlog;
#ifndef FINAL
  TB_CHECK(vlog->recording == true, "Visual Logger is not recording");
  vlog->recording = false;
#endif
}
void tb_vlog_clear(TbVisualLoggingSystem *vlog) {
  (void)vlog;
#ifndef FINAL
  TB_DYN_ARR_CLEAR(vlog->frames);
#endif
}

void tb_vlog_line(TbVisualLoggingSystem *vlog, float3 start, float3 end,
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

void tb_vlog_location(TbVisualLoggingSystem *vlog, float3 position,
                      float radius, float3 color) {
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
