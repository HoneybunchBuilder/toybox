#include "tb_ocean.slangh"
#include "tb_view.slangh"

[[vk::binding(0, 0)]]
ConstantBuffer<TbOceanData> ocean_data;
[[vk::push_constant]]
ConstantBuffer<TbOceanPushConstants> consts;

[[vk::binding(0, 1)]]
ConstantBuffer<TbViewData> camera_data;

struct VertexIn {
  int3 local_pos : POSITION;
  float4 inst_offset : POSITION0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  float3 tangent = float3(1, 0, 0);
  float3 binormal = float3(0, 0, 1);

  float3 pos = mul(consts.m, float4(i.local_pos, 1)).xyz + i.inst_offset.xyz;

  pos = calc_wave_pos(pos, ocean_data, tangent, binormal);
  float4 clip_pos = mul(camera_data.vp, float4(pos, 1));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

void frag(Interpolators i) {}
