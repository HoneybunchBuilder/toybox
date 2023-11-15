// Adapted heavily from https://catlikecoding.com/unity/tutorials/flow/waves/

#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"
#include "ocean.hlsli"
#include "pbr.hlsli"

ConstantBuffer<OceanData> ocean_data : register(b0, space0);
Texture2D depth_map : register(t1, space0);
Texture2D color_map : register(t2, space0);
sampler material_sampler : register(s3, space0);
SamplerComparisonState shadow_sampler : register(s4, space0);
[[vk::push_constant]] ConstantBuffer<OceanPushConstants> consts
    : register(b5, space0);

GLTF_VIEW_SET(space1);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  float4 inst_offset : POSITION_0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float4 screen_pos : POSITION2;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float4 clip : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  float3 tangent = float3(0, 0, 1);
  float3 binormal = float3(1, 0, 0);

  float3 pos = mul(consts.m, float4(i.local_pos, 1)).xyz + i.inst_offset.xyz;

  pos = calc_wave_pos(pos, ocean_data, tangent, binormal);
  float4 clip_pos = mul(camera_data.vp, float4(pos, 1));
  float4 world_pos = float4(pos, 1.0);

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.view_pos = mul(camera_data.v, world_pos).xyz;
  o.screen_pos = clip_to_screen(clip_pos);
  o.tangent = tangent;
  o.binormal = binormal;
  o.clip = clip_pos;

  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  float3 view_dir_vec = camera_data.view_pos - i.world_pos;

  // Calculate normal after interpolation
  float3 N = normalize(cross(normalize(i.tangent), normalize(i.binormal)));
  float3 V = normalize(view_dir_vec);
  float3 R = reflect(-V, N);
  float3 L = light_data.light_dir;
  float2 screen_uv = (i.clip.xy / i.clip.w) * 0.5 + 0.5;

  float3 albedo = float3(0, 0, 0);

  float2 uv = (i.screen_pos.xy) / i.screen_pos.w;

  // Underwater fog
  {
    const float near = camera_data.proj_params.x;
    const float far = camera_data.proj_params.y;
    // TODO: Paramaterize
    const float fog_density = 0.078f;
    const float3 fog_color = float3(0.095, 0.163, 0.282);

    // World position depth
    float scene_eye_depth =
        linear_depth(depth_map.Sample(material_sampler, uv).r, near, far);
    float fragment_eye_depth = -i.view_pos.z;
    float3 world_pos = camera_data.view_pos -
                       ((view_dir_vec / fragment_eye_depth) * scene_eye_depth);
    float depth_diff = world_pos.y - i.world_pos.y;

    float fog = saturate(exp(fog_density * depth_diff));
    float3 background_color = color_map.Sample(material_sampler, uv).rgb;
    albedo = lerp(fog_color, background_color, fog);
  }

  // Add a bit of a fresnel effect
  {
    // TODO: Parameterize
    const float horizon_dist = 5.0f;
    const float3 horizon_color = float3(0.8, 0.9, 0.8);

    float fresnel = dot(N, V);
    fresnel = pow(saturate(1 - fresnel), horizon_dist);
    albedo = lerp(albedo, horizon_color, fresnel);
  }

  // PBR Lighting
  float3 color = float3(0, 0, 0);
  {
    float metallic = 0.0;
    float roughness = 0.0;

    // Calculate shadow first
    float shadow = 1.0f;

    {
      Light l;
      l.light = light_data;
      l.shadow_map = shadow_map;
      l.shadow_sampler = shadow_sampler;

      Surface s;
      s.base_color = float4(albedo, 1);
      s.view_pos = i.view_pos;
      s.world_pos = i.world_pos;
      s.screen_uv = screen_uv;
      s.metallic = metallic;
      s.roughness = roughness;
      s.N = N;
      s.V = V;
      s.R = R;
      s.emissives = 0;
      shadow = max(shadow_visibility(l, s), (1 - AMBIENT));
    }

    // Lighting
    {
      float2 brdf =
          brdf_lut.Sample(brdf_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 reflection = prefiltered_reflection(
          prefiltered_map, filtered_env_sampler, R, roughness);
      float3 irradiance =
          irradiance_map.SampleLevel(filtered_env_sampler, N, 0).rgb;
      color = pbr_lighting(shadow, 1, albedo, metallic, roughness, brdf,
                           reflection, irradiance, light_data.color, L, V, N);
    }
  }

  // Subsurface Scattering
  if (L.y > 0) {
    float distortion = 0.4f;
    float power = 2.0f;
    float scale = 4.0f;
    float3 attenuation = 0.3f;
    float3 ambient = 0.1f;
    float3 sss_color = float3(0.13f, 0.69f, 0.67f);
    // Without handling thickness

    float3 H = normalize(L + N * distortion);
    float VdotH = pow(saturate(dot(V, -H)), power) * scale;
    float3 I = attenuation * (VdotH * ambient);

    color += (sss_color * light_data.color * I);
  }

  return float4(color, 1);
}
