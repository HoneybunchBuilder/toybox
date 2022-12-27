#include "visualloggingsystem.h"

#include "profiling.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "tbimgui.h"
#include "viewsystem.h"
#include "world.h"

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

typedef struct VLogDraw {
  VLogShapeType type;
  VLogShape shape;
} VLogDraw;

#define TB_MAX_VLOG_DRAWS 50

typedef struct VLogFrame {
  float timestamp;
  uint32_t draw_count;
  VLogDraw draws[TB_MAX_VLOG_DRAWS];
} VLogFrame;

typedef struct VLogDrawBatch {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  float4x4 vp_matrix;
  uint32_t draw_count;
  VLogDraw draws[TB_MAX_VLOG_DRAWS];
} VLogDrawBatch;

TB_DEFINE_SYSTEM(visual_logging, VisualLoggingSystem,
                 VisualLoggingSystemDescriptor)

void vlog_draw_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                      uint32_t batch_count, const void *batches) {
  TracyCZoneNC(ctx, "Visual Logger Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Visual Logger", 1, true);

  const VLogDrawBatch *vlog_batches = (const VLogDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    TracyCZoneNC(batch_ctx, "VLog Batch", TracyCategoryColorRendering, true);
    const VLogDrawBatch *batch = &vlog_batches[batch_idx];
    if (batch->draw_count == 0) {
      TracyCZoneEnd(batch_ctx);
      continue;
    }

    TracyCVkNamedZone(gpu_ctx, batch_scope, buffer, "VLog Batch", 2, true);
    cmd_begin_label(buffer, "VLog Batch", (float4){0.0f, 0.0f, 0.8f, 1.0f});

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      const VLogDraw *draw = &batch->draws[draw_idx];

      // Set push constants for draw

      if (draw->type == TB_VLOG_SHAPE_LOCATION) {
        // draw->shape.location;
      } else if (draw->type == TB_VLOG_SHAPE_LINE) {
        // draw->shape.line;
      }
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
      .dep_count = 0,
      .system_dep_count = 4,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = ViewSystemId,
      .system_deps[2] = RenderObjectSystemId,
      .system_deps[3] = RenderPipelineSystemId,
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

VLogDraw *frame_acquire_draw(VLogFrame *frame) {
  TB_CHECK_RETURN(frame->draw_count < TB_MAX_VLOG_DRAWS,
                  "Draw count exceeded frame max", NULL);
  return &frame->draws[frame->draw_count++];
}

VLogDraw *vlog_acquire_frame_draw(VisualLoggingSystem *vlog) {
  VLogFrame *frame = get_current_frame(vlog);
  return frame_acquire_draw(frame);
}

VkResult create_primitive_pipeline(VkPipelineLayout layout,
                                   VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

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
  RenderObjectSystem *render_object_system =
      tb_get_system(system_deps, system_dep_count, RenderObjectSystem);
  TB_CHECK_RETURN(
      render_object_system,
      "Failed to find render object system which visual logger depend on",
      false);
  RenderPipelineSystem *render_pipe_system =
      tb_get_system(system_deps, system_dep_count, RenderPipelineSystem);
  TB_CHECK_RETURN(
      render_pipe_system,
      "Failed to find render pipeline system which visual logger depend on",
      false);

  *self = (VisualLoggingSystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .render_system = render_system,
      .view_system = view_system,
      .render_object_system = render_object_system,
      .render_pipe_system = render_pipe_system,
  };

  // TODO: Load some default meshes, load some simple shader pipelines
  VkResult err = VK_SUCCESS;
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                        "Primitive Pipeline Layout",
                                        &self->pipe_layout);
  }

  err = create_primitive_pipeline(self->pipe_layout, &self->pipeline);
  TB_VK_CHECK_RET(err, "Failed to create primitive pipeline", false);

#endif
  return true;
}

void destroy_visual_logging_system(VisualLoggingSystem *self) {
#ifndef FINAL
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
  if (self->logging) {
    const VLogFrame *frame = &self->frames[self->log_frame_idx];

    //
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
  VLogDraw *draw = vlog_acquire_frame_draw(vlog);
  draw->type = TB_VLOG_SHAPE_LINE;
  draw->shape = (VLogShape){
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
  VLogDraw *draw = vlog_acquire_frame_draw(vlog);
  draw->type = TB_VLOG_SHAPE_LOCATION;
  draw->shape = (VLogShape){
      .location =
          {
              .position = position,
              .radius = radius,
              .color = color,
          },
  };
#endif
}
