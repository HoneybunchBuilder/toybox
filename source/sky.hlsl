// Adapted from: https://github.com/shff/opengl_sky
#include "common.hlsli"
#include "sky_common.hlsli"

#include "tb_simd.h"

ConstantBuffer<TbSkyData> sky_data
    : register(b0, space0); // Fragment Stage Only

[[vk::push_constant]]
ConstantBuffer<TbSkyPushConstants> consts : register(b1, space0);

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
  o.clip_pos = mul(consts.vp, float4(i.local_pos, 1.0));
  return o;
}

FragmentOut frag(Interpolators i) {
  float time = sky_data.time * 0.2f;
  float cirrus = sky_data.cirrus;
  float cumulus = sky_data.cumulus;
  float3 sun_dir = normalize(sky_data.sun_dir);
  float3 view_pos = i.view_pos;

  float3 color = sky(time, cirrus, cumulus, sun_dir, view_pos);

  FragmentOut o;
  o.color = float4(color, 1.0);
  // The skybox has no depth no matter what the geometry says
#ifdef TB_USE_INVERSE_DEPTH
  o.depth = 0;
#else
  o.depth = 1;
#endif

  return o;
}
