#include "common.hlsli"
#include "gltf.hlsli"
#include "shadow.hlsli"

// Indirection data
StructuredBuffer<int32_t> trans_indices : register(t0, space0);

// Per-view data
ConstantBuffer<TbCommonViewData> camera_data : register(b0, space1);

// Object data
StructuredBuffer<TbCommonObjectData> object_data : register(t0, space2);

// Mesh data
GLTF_MESH_SET(space3);

struct VertexIn {
  int32_t vert_idx : SV_VertexID;
  int32_t inst : SV_InstanceID;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i) {
  TbCommonObjectData obj_data = object_data[trans_indices[i.inst]];

  int3 local_pos = tb_vert_get_local_pos(obj_data.perm, i.vert_idx, pos_buffer);
  float3 normal = tb_vert_get_normal(obj_data.perm, i.vert_idx, norm_buffer);

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
