#include "common.hlsli"
#include "gltf.hlsli"

ConstantBuffer<TbCommonViewData> camera_data : register(b0, space0);
GLTF_DRAW_SET(space1);
TB_OBJECT_SET(space2);
TB_IDX_SET(space3);
TB_POS_SET(space4);
TB_NORM_SET(space5);

struct VertexIn {
  int32_t vert_idx : SV_VertexID;
  [[vk::builtin("DrawIndex")]]
  uint32_t draw_idx : POSITION0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i) {
  TbGLTFDrawData draw = draw_data[i.draw_idx];

  int32_t obj_idx = draw.obj_idx;
  TbCommonObjectData obj_data = tb_get_obj_data(obj_idx, object_data);

  int32_t mesh_idx = draw.mesh_idx;
  int32_t idx =
      tb_get_idx(i.vert_idx + draw.index_offset, mesh_idx, idx_buffers) +
      draw.vertex_offset;
  int3 local_pos = tb_vert_get_local_pos(draw.perm, idx, mesh_idx, pos_buffers);
  float3 normal = tb_vert_get_normal(draw.perm, idx, mesh_idx, norm_buffers);

  float3 world_pos = mul(obj_data.m, float4(local_pos, 1)).xyz;
  float3x3 orientation = (float3x3)obj_data.m;

  Interpolators o;
  o.clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));
  o.normal = mul(orientation, normal); // convert to world-space normal
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  float3 N = normalize(i.normal) * 0.5 + 0.5; // Convert to UNORM
  return float4(N, 1);
}
