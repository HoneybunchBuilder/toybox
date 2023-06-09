#include "common.hlsli"
#include "shadow.hlsli"

[[vk::push_constant]] ConstantBuffer<ShadowConstants> consts : register(b0);

struct VertexIn {
  int3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  float4x4 mvp = mul(consts.vp, consts.m);

  Interpolators o;
  o.clip_pos = mul(mvp, float4(i.local_pos, 1.0));
  return o;
}

void frag(Interpolators i) {}