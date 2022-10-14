#include "common.hlsli"
#include "cube_view_lut.hlsli"

TextureCube env_map : register(t0, space0); // Fragment Stage Only
sampler static_sampler : register(s1, space0); // Immutable Sampler

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

Interpolators vert(VertexIn i)
{
    float4x4 vp = view_proj_lut[i.view_idx];

    Interpolators o;
    o.view_pos = i.local_pos;
    o.view_pos.xy *= -1.0;
    o.clip_pos = mul(float4(i.local_pos, 1.0), vp);
    return o;
}

// See https://learnopengl.com/PBR/IBL/Diffuse-irradiance
float4 frag(Interpolators i) : SV_TARGET
{
  float3 normal = normalize(i.view_pos);

  float3 irradiance = float3(0, 0, 0);

  float3 up = float3(0, 1, 0);
  float3 right = normalize(cross(up, normal));
  up = normalize(cross(normal, right));

  const float sample_delta = 0.025;
  float samples = 0.0f;
  for(float phi = 0.0f; phi < 2.0f * PI; phi += sample_delta)
  {
    for(float theta = 0.0f; theta < 0.5 * PI; theta += sample_delta)
    {
      float3 tangent_sample = float3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
      float3 sample_vec = tangent_sample.x * right + tangent_sample.y * up + tangent_sample.z * normal;
      
      irradiance += env_map.Sample(static_sampler, sample_vec).rgb * cos(theta) * sin(theta);
      samples += 1.0f;
    }
  }
  irradiance = PI * irradiance * (1 / samples);

  return float4(irradiance, 1);
}
