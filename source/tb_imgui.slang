#include "tb_imgui.slangh"

[[vk::binding(0, 0)]]
Texture2D atlas;
[[vk::binding(1, 0)]]
SamplerState static_sampler;
[[vk::push_constant]]
ConstantBuffer<TbImGuiPushConstants> consts;

struct VertexIn {
  float2 position : POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  Interpolators o;
  o.clip_pos = float4(i.position * consts.scale + consts.translation, 0.5, 1);
  o.color = i.color;
  o.uv = i.uv;
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  return i.color * atlas.Sample(static_sampler, i.uv);
}
