#pragma once

#include "ocean.hlsli"
#include "common.hlsli"

ConstantBuffer<OceanData> ocean_data : register(b0, space0); // Vertex Stage Only
Texture2D depth_map : register(t1, space0); // Fragment Stage Only
sampler static_sampler : register(s2, space0); // Immutable Sampler
[[vk::push_constant]] 
ConstantBuffer<OceanPushConstants> consts : register(b3, space0); // Vertex Stage Only

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonViewData> camera_data: register(b0, space1);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    //float2 uv: TEXCOORD0;
};

struct Interpolators
{
  float4 clip_pos : SV_POSITION;
  float3 world_pos: POSITION0;
  //float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  //float2 uv: TEXCOORD0;
  //float4 shadowcoord : TEXCOORD1;
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
  OceanWave wave_0 = {0.24, 32, float2(0.8, -1)};
  OceanWave wave_1 = {0.28, 19, float2(-1, 0.6)};
  OceanWave wave_2 = {0.18, 23, float2(0.2, 3)};
  OceanWave wave_3 = {0.21, 20, float2(0.5, 1.7)};
  OceanWave wave_4 = {0.24, 16, float2(-0.6, .84)};

  float3 tangent = float3(1, 0, 0);
  float3 binormal = float3(0, 0, 1);
  float3 pos = i.local_pos;
  pos += gerstner_wave(wave_0, pos, tangent, binormal);
  pos += gerstner_wave(wave_1, pos, tangent, binormal);
  pos += gerstner_wave(wave_2, pos, tangent, binormal);
  pos += gerstner_wave(wave_3, pos, tangent, binormal);
  pos += gerstner_wave(wave_4, pos, tangent, binormal);

  float4 clip_pos = mul(float4(pos, 1.0), camera_data.vp);
  float4 world_pos = float4(pos, 1.0);

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.tangent = tangent;
  o.binormal = binormal;
  //o.uv = i.uv;

  return o;
}
