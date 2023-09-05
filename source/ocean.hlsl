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
sampler shadow_sampler : register(s4, space0);
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
  float4 clip : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  float3 pos = mul(consts.m, float4(i.local_pos, 1)).xyz + i.inst_offset.xyz;
  pos = calc_wave_pos(pos.xz, ocean_data);

  float4 clip_pos = mul(camera_data.vp, float4(pos, 1));
  float4 world_pos = float4(pos, 1.0);

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.view_pos = mul(camera_data.v, world_pos).xyz;
  o.screen_pos = clip_to_screen(clip_pos);
  o.clip = clip_pos;

  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  float3 view_dir_vec = camera_data.view_pos - i.world_pos;

  // Calculate normal after interpolation
  float3 N = normalize(calc_wave_normal(i.world_pos.xz, ocean_data));
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
    const float fog_density = 0.2f;
    const float3 fog_color = float3(0.105, 0.163, 0.262);

    // World position depth
    float scene_eye_depth =
        linear_depth(depth_map.Sample(material_sampler, uv).r, near, far);
    float fragment_eye_depth = -i.view_pos.z;
    float3 world_pos = camera_data.view_pos -
                       ((view_dir_vec / fragment_eye_depth) * scene_eye_depth);
    float depth_diff = world_pos.y;

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

    // Lighting
    {
      float2 brdf =
          brdf_lut.Sample(brdf_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 reflection = prefiltered_reflection(
          prefiltered_map, filtered_env_sampler, R, roughness);
      float3 irradiance =
          irradiance_map.SampleLevel(filtered_env_sampler, N, 0).rgb;
      float ao = ssao_map.Sample(shadow_sampler, screen_uv).r;
      color = pbr_lighting(ao, albedo, metallic, roughness, brdf, reflection,
                           irradiance, light_data.color, L, V, N);
    }
  }

  // Shadow cascades
  {
    uint cascade_idx = 0;
    for (uint c = 0; c < (TB_CASCADE_COUNT - 1); ++c) {
      if (i.view_pos.z < light_data.cascade_splits[c]) {
        cascade_idx = c + 1;
      }
    }

    float4 shadow_coord =
        mul(light_data.cascade_vps[cascade_idx], float4(i.world_pos, 1.0));

    float shadow =
        pcf_filter(shadow_coord, shadow_map, cascade_idx, shadow_sampler);
    color *= shadow;
  }

  return float4(color, 1);
}
