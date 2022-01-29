#include "common.hlsli"
#include "sky_common.hlsli"

ConstantBuffer<SkyData> sky_data : register(b0, space0); // Fragment Stage Only

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    uint view_idx : SV_ViewID;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 view_pos : TEXCOORD0;
};

// Per view matrix look up table
// So that each view is pointing at the right face of the cubemap
// Generated manually by doing the math on the CPU and writing the values here
static const float4x4 view_proj_lut[6] = {
    // X+
    {0.00000000, 0.00000000, 0.00001, -1.00000000,
     0.00000000, -0.811820984, 0.00000000, 0.00000000,
     -0.811820984, 0.00000000, 0.00000000, 0.00000000,
     0.00000000, 0.00000000, 0.0100000994, 0.00000000,},
    // X-
    {0.00000000, 0.00000000, 0.00001, 1.00000000,
     0.00000000, -0.811820984, 0.00000000, 0.00000000,
     0.811820984, 0.00000000, 0.00000000, 0.00000000,
     0.00000000, 0.00000000, 0.0100000994, 0.00000000,},
    // Y+
    {-0.811820984, 0.00000000, 0.00000000, 0.00000000,
     0.00000000, 0.00000000, 0.00001, -1.0,
     0.00000000, -0.811820984, 0.00000000, 0.00000000,
     0.00000000, 0.00000000, 0.01, 0.00000000,},
    // Y-
    {0.811820984, 0.00000000, 0.00000000, 0.00000000,
     0.00000000, 0.00000000, -0.00001, 1.0,
     0.00000000, -0.811820984, 0.00000000, 0.00000000,
     0.00000000, 0.00000000, 0.01, 0.00000000,},
    // Z+
    {-0.81182, 0.000000, 0.00000, 0.00,
     0.00000, -0.81182, 0.00000, 0.00,
     0.00000, 0.000000, -0.00001, 1.0000,
     0.00000, 0.000000, 0.01, 0.00,},
    // Z-
    {0.81182, 0.000000, 0.00000, 0.00,
     0.00000, -0.81182, 0.00000, 0.00,
     0.00000, 0.000000, 0.00001, -1.0000,
     0.00000, 0.000000, 0.01, 0.00,},
};

Interpolators vert(VertexIn i)
{
    float4x4 vp = view_proj_lut[i.view_idx];

    Interpolators o;
    o.view_pos = i.local_pos;
    o.view_pos.xy *= -1.0;
    o.clip_pos = mul(float4(i.local_pos, 1.0), vp);
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