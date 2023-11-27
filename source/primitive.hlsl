#include "common.hlsli"

[[vk::push_constant]] ConstantBuffer<TbPrimitivePushConstants> consts
    : register(b0, space0);
ConstantBuffer<TbCommonViewData> camera_data : register(b0, space0);

struct VertexIn {
  int3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  float3 world_pos = (i.local_pos * consts.scale) + consts.position;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

float4 frag(Interpolators i) : SV_TARGET { return consts.color; }