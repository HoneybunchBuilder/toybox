// Adapted from: https://github.com/shff/opengl_sky
#include "common.hlsli"
#include "sky_common.hlsli"

ConstantBuffer<SkyData> sky_data : register(b0, space0); // Fragment Stage Only

[[vk::push_constant]] ConstantBuffer<SkyPushConstants> consts
    : register(b1, space0);

struct VertexIn {
  float3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 view_pos : POSITION0;
};

struct FragmentOut {
  float4 color : SV_TARGET;
  float depth : SV_DEPTH;
};

Interpolators vert(VertexIn i) {
  Interpolators o;
  o.view_pos = i.local_pos;
  o.clip_pos = mul(float4(i.local_pos, 1.0), consts.vp);
  return o;
}

FragmentOut frag(Interpolators i) {
  float time = sky_data.time * 0.2f;
  float cirrus = sky_data.cirrus;
  float cumulus = sky_data.cumulus;
  float3 sun_dir = normalize(float3(0.707, 0.707, 0)); // sky_data.sun_dir;
  float3 view_pos = i.view_pos;

  float3 color = sky(time, cirrus, cumulus, sun_dir, view_pos);

  FragmentOut o;
  o.color = float4(color, 1.0);
  o.depth = 0; // The skybox has no depth no matter what the geometry says

  return o;
}
