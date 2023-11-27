#include "common.hlsli"
#include "shadow.hlsli"

// Indirection data
StructuredBuffer<int32_t> trans_indices : register(t0, space0);

// Per-view data
ConstantBuffer<TbCommonViewData> camera_data : register(b0, space1);

// Object data
StructuredBuffer<TbCommonObjectData> object_data : register(t0, space2);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  int inst : SV_InstanceID;
  half3 normal : NORMAL0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i) {
  int32_t trans_idx = trans_indices[i.inst];
  float4x4 m = object_data[trans_idx].m;
  float3 world_pos = mul(m, float4(i.local_pos, 1)).xyz;
  float3x3 orientation = (float3x3)m;

  Interpolators o;
  o.clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));
  o.normal = mul(orientation, i.normal); // convert to world-space normal
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  float3 N = normalize(i.normal) * 0.5 + 0.5; // Convert to UNORM
  return float4(N, 1);
}