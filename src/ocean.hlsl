// Adapted heavily from https://catlikecoding.com/unity/tutorials/flow/waves/

#include "common.hlsli"
#include "lighting.hlsli"
#include "ocean.hlsli"
#include "pbr.hlsli"

ConstantBuffer<OceanData> ocean_data
    : register(b0, space0);                    // Vertex Stage Only
Texture2D depth_map : register(t1, space0);    // Fragment Stage Only
Texture2D color_map : register(t2, space0);    // Fragment Stage Only
sampler static_sampler : register(s3, space0); // Immutable Sampler

// Per-view data
ConstantBuffer<CommonViewData> camera_data : register(b0, space1);
TextureCube irradiance_map : register(t1, space1);
TextureCube prefiltered_map : register(t2, space1);
Texture2D brdf_lut : register(t3, space1);
ConstantBuffer<CommonLightData> light_data : register(b4, space1);
Texture2D shadow_maps[CASCADE_COUNT] : register(t5, space1); // Frag Only

struct VertexIn {
  int3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float4 screen_pos : POSITION2;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
};

Interpolators vert(VertexIn i) {
  float3 tangent = float3(1, 0, 0);
  float3 binormal = float3(0, 0, 1);
  float3 pos = calc_wave_pos(i.local_pos, ocean_data.m, ocean_data.time,
                             tangent, binormal);
  float4 clip_pos = mul(float4(pos, 1), camera_data.vp);
  float4 world_pos = float4(pos, 1.0);

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.view_pos = mul(world_pos, camera_data.v).xyz;
  o.screen_pos = clip_to_screen(clip_pos);
  o.tangent = tangent;
  o.binormal = binormal;

  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  // Calculate normal after interpolation
  float3 N = normalize(cross(normalize(i.binormal), normalize(i.tangent)));
  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float3 R = reflect(-V, N);
  float3 L = light_data.light_dir;

  float3 albedo = float3(0, 0, 0);

  // Underwater fog
  {
    // HACK: Need to paramaterize these
    const float near = 0.1f;
    const float far = 1000.0f;
    const float fog_density = 0.015f;
    const float3 fog_color = float3(0.305, 0.513, 0.662);
    const float refraction_strength = 0.25f;

    const float2 uv_offset = N.xy * refraction_strength;
    const float2 uv = (i.screen_pos.xy + uv_offset) / i.screen_pos.w;

    float background_depth =
        1.0f - linear_depth(depth_map.Sample(static_sampler, uv).r, near, far);
    float surface_depth = 1.0f - depth_from_clip_z(i.screen_pos.z, near, far);

    float depth_diff = surface_depth - background_depth;

    float3 background_color = color_map.Sample(static_sampler, uv).rgb;
    float fog = exp2(fog_density * depth_diff);

    albedo = lerp(fog_color, background_color, fog);
  }

  // PBR Lighting
  float3 color = float3(0, 0, 0);
  {
    float metallic = 0.0;
    float roughness = 0.0;

    // Lighting
    {
      float2 brdf =
          brdf_lut
              .Sample(static_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 reflection =
          prefiltered_reflection(prefiltered_map, static_sampler, R, roughness);
      float3 irradiance = irradiance_map.Sample(static_sampler, N).rgb;
      color = pbr_lighting(albedo, metallic, roughness, brdf, reflection,
                               irradiance, light_data.color, L, V, N);
    }
  }

  // Shadow cascades
  {
    uint cascade_idx = 0;
    for (uint c = 0; c < (CASCADE_COUNT - 1); ++c) {
      if (i.view_pos.z < light_data.cascade_splits[c]) {
        cascade_idx = c + 1;
      }
    }

    float4 shadow_coord =
        mul(float4(i.world_pos, 1.0), light_data.cascade_vps[cascade_idx]);

    float NdotL = clamp(dot(N, L), 0.001, 1.0);
    float shadow = pcf_filter(shadow_coord, AMBIENT, shadow_maps[cascade_idx],
                              static_sampler, NdotL);
    color *= shadow;
  }

  return float4(color, 1);
}
