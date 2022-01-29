#include "common.hlsli"
#include "sky_common.hlsli"

ConstantBuffer<SkyData> sky_data : register(b0, space0); // Fragment Stage Only

struct VertexIn
{
    uint vert_idx : SV_VertexID;
    uint view_idx : SV_ViewID;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 view_pos : TEXCOORD0;
};

Interpolators vert(VertexIn i)
{
    Interpolators o;
    o.view_pos = float3((i.vert_idx << 1) & 2, i.vert_idx & 2, i.view_idx);
    o.clip_pos = float4(o.view_pos.xy * 2.0f + -1.0f, 0.5f, 1.0f);
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
  float time = sky_data.time * 0.2f;
  float cirrus = sky_data.cirrus;
  float cumulus = sky_data.cumulus;
  float3 sun_dir = sky_data.sun_dir;
  float3 view_pos = i.view_pos;
  
  float3 color = sky(time, cirrus, cumulus, sun_dir, view_pos);

  return float4(color, 1.0);
}