#include "tb_common.slangh"
#include "tb_gltf.slangh"
#include "tb_intrin.slangh"

GLTF_VIEW_SET(0);
GLTF_DRAW_SET(1);
TB_OBJECT_SET(2);
TB_IDX_SET(3)
TB_POS_SET(4);

struct VertexIn {
  int32_t vert_idx : SV_VertexID;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  uint32_t draw_idx = tb_get_draw_index();
  TbGLTFDrawData draw = tb_get_gltf_draw_data(draw_idx, draw_data);
  uint32_t obj_idx = draw.obj_idx;
  uint32_t mesh_idx = draw.mesh_idx;
  TbCommonObjectData obj_data = tb_get_obj_data(obj_idx, object_data);

  int32_t idx =
      tb_get_idx(i.vert_idx + draw.index_offset, mesh_idx, idx_buffers) +
      draw.vertex_offset;
  int3 local_pos = tb_vert_get_local_pos(draw.perm, idx, mesh_idx, pos_buffers);

  float3 world_pos = mul(obj_data.m, float4(local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

void frag(Interpolators i) {}
