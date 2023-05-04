#include "common.hlsli"
#include "shadow.hlsli"

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data : register(b0, space0);

// Per-view data
ConstantBuffer<CommonViewData> camera_data : register(b0, space1);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  half3 normal : NORMAL0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i) {
  float3 world_pos = mul(float4(i.local_pos, 1), object_data.m).xyz;

  float3x3 orientation = (float3x3)object_data.m;

  Interpolators o;
  o.clip_pos = mul(float4(world_pos, 1.0), camera_data.vp);
  o.normal = mul(i.normal, orientation); // convert to world-space normal
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  float3 N = normalize(i.normal) * 0.5 + 0.5; // Convert to UNORM
  return float4(N, 1);
}