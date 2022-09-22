// Adapted heavily from https://catlikecoding.com/unity/tutorials/flow/waves/

#include "common.hlsli"
#include "pbr.hlsli"
#include "ocean.hlsli"
#include "lighting.hlsli"

// Wave data - Vertex & Fragment Stages
ConstantBuffer<OceanData> ocean_data : register(b0, space0);
[[vk::push_constant]] // Vertex stage only
ConstantBuffer<OceanPushConstants> consts : register(b1, space0);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonViewData> camera_data: register(b0, space1);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float2 uv: TEXCOORD0;
};

struct Interpolators
{
  float4 clip_pos : SV_POSITION;
  float3 world_pos: POSITION0;
  float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float2 uv: TEXCOORD0;
  float4 shadowcoord : TEXCOORD1;
};

float3 gerstner_wave(OceanWave wave, float3 p, inout float3 tangent, inout float3 binormal)
{
  float steepness = wave.steepness;
  float k = 2 * PI / wave.wavelength;
  float c = sqrt(9.8 / k);
  float2 d = normalize(wave.direction);
  float f = k * (dot(d, p.xz) - c * consts.time);
  float a = steepness / k;

  float sinf = sin(f);
  float cosf = cos(f);

  tangent += float3(
    -d.x * d.x * (steepness * sinf),
    d.x * (steepness * cosf),
    -d.x * d.y * (steepness * sinf)
  );
  binormal += float3(
    -d.x * d.y * (steepness * sinf),
    d.y * (steepness * cosf),
    -d.y * d.y * (steepness * sinf)
  );
  return float3(
    d.x * (a * cosf),
    a * sinf,
    d.y * (a * cosf)
  );
}

Interpolators vert(VertexIn i)
{
  OceanWave wave_0 = {0.25, 60, float2(0.7, 1)};
  OceanWave wave_1 = {0.25, 31, float2(1, 0.6)};
  OceanWave wave_2 = {0.25, 18, float2(1, 1.3)};

  float3 tangent = float3(1, 0, 0);
  float3 binormal = float3(0, 0, 1);
  float3 pos = i.local_pos;
  pos += gerstner_wave(wave_0, pos, tangent, binormal);
  pos += gerstner_wave(wave_1, pos, tangent, binormal);
  pos += gerstner_wave(wave_2, pos, tangent, binormal);

  // Calculate normal
  float3 normal = normalize(cross(binormal, tangent));

  float4 clip_pos = mul(float4(pos, 1.0), camera_data.vp);
  float4 world_pos = float4(pos, 1.0);

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.normal = normal;
  o.tangent = tangent;
  o.binormal = binormal;
  o.uv = i.uv;

  return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
  float3 base_color = float3(0.1, 0.4, 0.7);
  float3 light_dir = float3(0, 1, 0);

  float3 out_color = float3(0.0f, 0.0f, 0.0f);

  float3 V = normalize(camera_data.view_pos - i.world_pos);

  float gloss = 0.5;

  // for each light
  {
    float3 N = normalize(i.normal);
    float3 L = light_dir;
    float3 H = normalize(V + L);

    float3 light_color = float3(1, 1, 1);
    out_color += phong_light(base_color, light_color, gloss, N, L, V, H);
  }

  return float4(out_color, 1);
}
