#include "ocean.hlsli"
#include "shadow.hlsli"

// Per-object data: Vertex Stage Only
ConstantBuffer<OceanData> ocean_data : register(b0, space0);

[[vk::push_constant]] ConstantBuffer<OceanShadowConstants> consts
    : register(b0, space0);

struct VertexIn {
  int3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  float3 tangent = float3(1, 0, 0);
  float3 binormal = float3(0, 0, 1);
  float3 pos =
      calc_wave_pos(i.local_pos, consts.m, ocean_data.time, tangent, binormal);
  float4 clip_pos = mul(consts.vp, float4(pos, 1));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

void frag(Interpolators i) {}