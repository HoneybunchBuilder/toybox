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
  float4x4 mvp = mul(consts.m, consts.vp);

  Interpolators o;
  o.clip_pos = mul(float4(i.local_pos, 1.0), mvp);
  return o;
}

void frag(Interpolators i) {}