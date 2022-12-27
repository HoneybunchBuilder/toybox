#include "common.hlsli"

[[vk::push_constant]] 
ConstantBuffer<PrimitivePushConstants> consts : register(b0, space0);

ConstantBuffer<CommonObjectData> object_data : register(b0, space1);

ConstantBuffer<CommonViewData> camera_data : register(b0, space2);

struct VertexIn {
  int3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  float3 world_pos = mul(float4(i.local_pos, 1), object_data.m).xyz;
  float4 clip_pos = mul(float4(world_pos, 1.0), camera_data.vp);

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  return consts.color;
}