#include "common.hlsli"
#include "ocean.hlsli"

ConstantBuffer<OceanData> ocean_data : register(b0, space0);
[[vk::push_constant]] ConstantBuffer<OceanPushConstants> consts
    : register(b1, space0);

ConstantBuffer<CommonViewData> camera_data : register(b0, space1);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  float4 inst_offset : POSITION_0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  float3 tangent = float3(1, 0, 0);
  float3 binormal = float3(0, 0, 1);
  float3 pos = calc_wave_pos(i.local_pos, consts.m, i.inst_offset.xyz,
                             ocean_data.time, tangent, binormal);
  float4 clip_pos = mul(camera_data.vp, float4(pos, 1));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

void frag(Interpolators i) {}