#pragma once

#include "allocator.h"
#include "simd.h"

#define VisualLoggingSystemId 0x1337F001

typedef struct SystemDescriptor SystemDescriptor;

typedef struct VisualLoggingSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} VisualLoggingSystemDescriptor;

typedef struct VLogFrame VLogFrame;

typedef struct RenderSystem RenderSystem;
typedef struct ViewSystem ViewSystem;
typedef struct RenderObjectSystem RenderObjectSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct MeshSystem MeshSystem;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkBuffer_T *VkBuffer;
typedef uint32_t TbDrawContextId;
typedef uint64_t TbMeshId;

typedef struct VisualLoggingSystem {
  Allocator tmp_alloc;
  Allocator std_alloc;

  RenderSystem *render_system;
  ViewSystem *view_system;
  RenderPipelineSystem *render_pipe_system;
  MeshSystem *mesh_system;

  TbMeshId sphere_mesh;
  uint32_t sphere_index_type;
  uint32_t sphere_index_count;
  uint32_t sphere_pos_offset;
  float3 sphere_scale;
  VkBuffer sphere_geom_buffer;

  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDrawContextId draw_ctx;

  bool logging;
  int32_t log_frame_idx;

  bool recording;
  uint32_t frame_count;
  VLogFrame *frames;
  uint32_t frame_max;

} VisualLoggingSystem;

void tb_visual_logging_system_descriptor(
    SystemDescriptor *desc, const VisualLoggingSystemDescriptor *vlog_desc);

void tb_vlog_begin_recording(VisualLoggingSystem *vlog);
void tb_vlog_end_recording(VisualLoggingSystem *vlog);
void tb_vlog_clear(VisualLoggingSystem *vlog);

void tb_vlog_line(VisualLoggingSystem *vlog, float3 start, float3 end,
                  float3 color);
void tb_vlog_location(VisualLoggingSystem *vlog, float3 position, float radius,
                      float3 color);