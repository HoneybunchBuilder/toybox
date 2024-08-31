#include "tb_cube_view_lut.slangh"
#include "tb_sky.slangh"

[[vk::binding(0, 0)]]
ConstantBuffer<TbSkyData> sky_data;

struct VertexIn {
  float3 local_pos : POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 view_pos : TEXCOORD0;
};

Interpolators vert(VertexIn i, uint32_t view_idx: SV_ViewID) {
  float4x4 vp = view_proj_lut[view_idx];

  Interpolators o;
  o.view_pos = i.local_pos;
  o.clip_pos = mul(vp, float4(i.local_pos, 1.0));
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  float time = sky_data.time * 0.02f;
  float cirrus = sky_data.cirrus;
  float cumulus = sky_data.cumulus;
  float3 sun_dir = normalize(sky_data.sun_dir);
  float3 view_pos = i.view_pos;

  float3 color = sky(time, cirrus, cumulus, sun_dir, view_pos);

  return float4(color, 1.0);
}
