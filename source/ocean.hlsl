// Adapted heavily from https://catlikecoding.com/unity/tutorials/flow/waves/

#include "common.hlsli"
#include "lighting.hlsli"
#include "ocean.hlsli"
#include "pbr.hlsli"

ConstantBuffer<OceanData> ocean_data : register(b0, space0);
Texture2D depth_map : register(t1, space0);
Texture2D color_map : register(t2, space0);
sampler static_sampler : register(s3, space0);

// Per-view data
ConstantBuffer<CommonViewData> camera_data : register(b0, space1);
TextureCube irradiance_map : register(t1, space1);
TextureCube prefiltered_map : register(t2, space1);
Texture2D brdf_lut : register(t3, space1);
ConstantBuffer<CommonLightData> light_data : register(b4, space1);
Texture2D shadow_map : register(t5, space1);
Texture2D ssao_map : register(s6, space1);

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
  float4 clip : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  float3 tangent = float3(0, 0, 1);
  float3 binormal = float3(1, 0, 0);
  float3 pos = calc_wave_pos(i.local_pos, ocean_data.m, ocean_data.time,
                             tangent, binormal);
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

  // Calculate refracted UVs
  float2 uv = float2(0, 0);
  {
    // TODO: Paramaterize
    const float refraction_scale = 5.0f;
    const float refraction_speed = 0.3f;
    const float refraction_strength = 1.0f;

    float scale = 1.0f / refraction_scale;
    float speed = refraction_speed * ocean_data.time;

    // Note: parent scope variable "uv" is used as a temporary here.
    // It will be overwritten with a value of a different context later
    uv = tiling_and_offset(N.xz, float2(scale, scale), float2(speed, speed));

    float ripple = gradient_noise(uv);
    ripple = remap(0, 1, -1, 1, ripple);
    ripple *= refraction_strength;

    float4 pos = i.screen_pos + float4(ripple, ripple, ripple, ripple);

    float2 offset = N.xy * refraction_strength;
    uv = (pos.xy + offset) / pos.w;
  }

  // Underwater fog
  {
    const float near = camera_data.proj_params.x;
    const float far = camera_data.proj_params.y;
    // TODO: Paramaterize
    const float fog_density = 0.05f;
    const float3 fog_color = float3(0.105, 0.163, 0.262);

    // World position depth
    float raw_depth = depth_map.Sample(static_sampler, uv).r;
    float scene_eye_depth = linear_depth(raw_depth, near, far);
    float fragment_eye_depth = -i.view_pos.z;
    float3 world_pos = camera_data.view_pos -
                       ((view_dir_vec / fragment_eye_depth) * scene_eye_depth);
    float depth_diff = world_pos.y;

    float fog = saturate(exp(fog_density * depth_diff));
    float3 background_color = color_map.Sample(static_sampler, uv).rgb;
    albedo = lerp(fog_color, background_color, fog);
  }

  // Add a bit of a fresnel effect
  {
    // TODO: Parameterize
    const float horizon_dist = 5.0f;
    const float3 horizon_color = float3(0.3, 0.9, 0.3);

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
          brdf_lut
              .Sample(static_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 reflection =
          prefiltered_reflection(prefiltered_map, static_sampler, R, roughness);
      float3 irradiance = irradiance_map.Sample(static_sampler, N).rgb;
      float ao = ssao_map.Sample(static_sampler, screen_uv).r;
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

    float shadow = pcf_filter(shadow_coord, AMBIENT, shadow_map, cascade_idx,
                              static_sampler);
    color *= shadow;
  }

  return float4(color, 1);
}
