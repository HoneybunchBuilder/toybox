#pragma once

#include "pbr.hlsli"

#define AMBIENT 0.3

struct View {
  TextureCube irradiance_map;
  TextureCube prefiltered_map;
  Texture2D brdf_lut;
  sampler filtered_env_sampler;
  sampler brdf_sampler;
};

struct Light {
  CommonLightData light;
  Texture2DArray shadow_map;
  SamplerComparisonState shadow_sampler;
};

struct Surface {
  float4 base_color;
  float3 view_pos;
  float3 world_pos;
  float2 screen_uv;
  float metallic;
  float roughness;
  float3 N;
  float3 V;
  float3 R;
  float4 emissives;
};

float3 pbr_direct(float NdotV, float3 F0, float3 N, float3 V, float3 L,
                  float3 light_radiance, float3 albedo, float metallic,
                  float roughness) {
  float3 direct = 0;

  // TODO: For each light
  {
    float3 H = normalize(V + L);
    float distance = 1.0f; // TODO: account for attenuation
    float attenuation = 1.0f / (distance * distance);
    float3 radiance = light_radiance * attenuation;

    float NdotH = max(dot(N, H), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float HdotV = max(dot(H, V), 0.0f);

    // Fresnel from direct lighting
    float3 F = fresnel_schlick(HdotV, F0);
    // Normal distribution for specular brdf
    float D = microfacet_distribution(roughness, NdotH);
    // Geometric attenuation for specular brdf
    float G = geometric_occlusion(NdotL, NdotV, roughness);

    // Diffuse scattering
    float3 kd = lerp(1 - F, 0, metallic);

    // Lambert diffuse brdf
    float3 diffuse = kd * albedo;

    // Cook-Torrance specular BRDF
    float3 specular = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);

    direct += (diffuse + specular) * radiance * NdotL;
  }

  return direct;
}

float3 pbr_ambient(float NdotV, float3 F0, float3 irradiance, float3 reflection,
                   float2 brdf, float3 albedo, float metallic, float roughness,
                   float shadow) {
  float3 F = fresnel_schlick_roughness(NdotV, F0, roughness);

  float3 kd = lerp(1.0f - F, 0.0f, metallic * shadow);

  float3 diffuse_ibl = kd * albedo * irradiance;

  // No specular component if shadowed
  float3 specular_ibl = (F0 * brdf.x + brdf.y) * reflection * shadow;

  return diffuse_ibl + specular_ibl;
}

float3 pbr_lighting(float shadow, float ao, float3 albedo, float metallic,
                    float roughness, float2 brdf, float3 reflection,
                    float3 irradiance, float3 light_color, float3 L, float3 V,
                    float3 N) {
  // HACK
  // We're only working with one directional light right now so this is fine
  // we don't want light to come from an object that is behind the horizon
  // However this shouldn't be handled here but rather
  // as a property of directional lights
  // Later
  if (L.y < 0) {
    return 0;
  }

  float3 F0 = lerp(0.04, albedo, metallic);
  float NdotV = max(dot(N, V), 0);

  float3 ambient = pbr_ambient(NdotV, F0, irradiance, reflection, brdf, albedo,
                               metallic, roughness, shadow) *
                   ao;
  float3 direct =
      pbr_direct(NdotV, F0, N, V, L, light_color, albedo, metallic, roughness);

  return (direct * shadow) + ambient;
}

float2 compute_plane_depth_bias(float3 dx, float3 dy) {
  float2 bias_uv = float2(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z);
  bias_uv *= 1.0f / ((dx.x * dy.y) - (dx.y * dy.x));
  return bias_uv;
}

float sample_shadow_map(Texture2DArray shadow_map, SamplerComparisonState samp,
                        float2 base_uv, float u, float v,
                        float2 shadow_map_size_inv, uint cascade_idx,
                        float depth) {
  float2 uv = base_uv + float2(u, v) * shadow_map_size_inv;
  float z = depth;
  return shadow_map.SampleCmpLevelZero(samp, float3(uv, cascade_idx), z);
}

float sample_shadow_pcf(Texture2DArray shadow_map, SamplerComparisonState samp,
                        float3 shadow_pos, float3 shadow_pos_dx,
                        float3 shadow_pos_dy, uint cascade_idx) {
  float2 shadow_map_size;
  float cascade_count;
  shadow_map.GetDimensions(shadow_map_size.x, shadow_map_size.y, cascade_count);

  float light_depth = shadow_pos.z;

#if 0
  float2 texel_size = 1.0f / shadow_map_size;
  float2 plane_depth_bias =
      compute_plane_depth_bias(shadow_pos_dx, shadow_pos_dy);

  // Static depth biasing to make up for incorrect fractional sampling on the
  // shadow map grid
  float error = 2 * dot(float2(1, 1) * texel_size, abs(plane_depth_bias));
  light_depth -= max(abs(error), 0.01f);
#else
  float2 plane_depth_bias = 0;
  const float bias = 0.002f;
  light_depth -= bias;
#endif

  float2 uv = shadow_pos.xy * shadow_map_size;
  float2 shadow_map_size_inv = 1.0f / shadow_map_size;

  float2 base_uv = float2(floor(uv.x + 0.5f), floor(uv.y + 0.5f));

  float s = (uv.x + 0.5 - base_uv.x);
  float t = (uv.y + 0.5 - base_uv.y);

  base_uv -= float2(0.5, 0.5);
  base_uv *= shadow_map_size_inv;

  float sum = 0;
#if 0
  // Single sample
  sum = shadow_map.SampleCmpLevelZero(samp, float3(shadow_pos.xy, cascade_idx),
                                      light_depth);
#else
  // PCF Filter Size 7
  float uw0 = (5 * s - 6);
  float uw1 = (11 * s - 28);
  float uw2 = -(11 * s + 17);
  float uw3 = -(5 * s + 1);

  float u0 = (4 * s - 5) / uw0 - 3;
  float u1 = (4 * s - 16) / uw1 - 1;
  float u2 = -(7 * s + 5) / uw2 + 1;
  float u3 = -s / uw3 + 3;

  float vw0 = (5 * t - 6);
  float vw1 = (11 * t - 28);
  float vw2 = -(11 * t + 17);
  float vw3 = -(5 * t + 1);

  float v0 = (4 * t - 5) / vw0 - 3;
  float v1 = (4 * t - 16) / vw1 - 1;
  float v2 = -(7 * t + 5) / vw2 + 1;
  float v3 = -t / vw3 + 3;

  sum += uw0 * vw0 *
         sample_shadow_map(shadow_map, samp, base_uv, u0, v0,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw1 * vw0 *
         sample_shadow_map(shadow_map, samp, base_uv, u1, v0,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw2 * vw0 *
         sample_shadow_map(shadow_map, samp, base_uv, u2, v0,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw3 * vw0 *
         sample_shadow_map(shadow_map, samp, base_uv, u3, v0,
                           shadow_map_size_inv, cascade_idx, light_depth);

  sum += uw0 * vw1 *
         sample_shadow_map(shadow_map, samp, base_uv, u0, v1,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw1 * vw1 *
         sample_shadow_map(shadow_map, samp, base_uv, u1, v1,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw2 * vw1 *
         sample_shadow_map(shadow_map, samp, base_uv, u2, v1,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw3 * vw1 *
         sample_shadow_map(shadow_map, samp, base_uv, u3, v1,
                           shadow_map_size_inv, cascade_idx, light_depth);

  sum += uw0 * vw2 *
         sample_shadow_map(shadow_map, samp, base_uv, u0, v2,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw1 * vw2 *
         sample_shadow_map(shadow_map, samp, base_uv, u1, v2,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw2 * vw2 *
         sample_shadow_map(shadow_map, samp, base_uv, u2, v2,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw3 * vw2 *
         sample_shadow_map(shadow_map, samp, base_uv, u3, v2,
                           shadow_map_size_inv, cascade_idx, light_depth);

  sum += uw0 * vw3 *
         sample_shadow_map(shadow_map, samp, base_uv, u0, v3,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw1 * vw3 *
         sample_shadow_map(shadow_map, samp, base_uv, u1, v3,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw2 * vw3 *
         sample_shadow_map(shadow_map, samp, base_uv, u2, v3,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum += uw3 * vw3 *
         sample_shadow_map(shadow_map, samp, base_uv, u3, v3,
                           shadow_map_size_inv, cascade_idx, light_depth);
  sum *= 1.0f / 2704;
#endif
  if (sum < light_depth) {
    return sum;
  } else {
    return 1;
  }
}

float sample_shadow_cascade(Texture2DArray shadow_map,
                            SamplerComparisonState samp, float3 shadow_pos,
                            float3 shadow_pos_dx, float3 shadow_pos_dy,
                            float4x4 shadow_mat, uint cascade_idx) {
  // Need to transform derivatives by the shadow mat too
  float4 proj_pos = mul(shadow_mat, float4(shadow_pos_dx, 1.0f));
  shadow_pos_dx = proj_pos.xyz / proj_pos.w;
  shadow_pos_dx.xy = shadow_pos_dx.xy * 0.5 + 0.5;

  proj_pos = mul(shadow_mat, float4(shadow_pos_dy, 1.0f));
  shadow_pos_dy = proj_pos.xyz / proj_pos.w;
  shadow_pos_dy.xy = shadow_pos_dy.xy * 0.5 + 0.5;

  // Perform specific filtering routine
  return sample_shadow_pcf(shadow_map, samp, shadow_pos, shadow_pos_dx,
                           shadow_pos_dy, cascade_idx);
}

float shadow_visibility(Light l, Surface s) {

  uint cascade_idx = TB_CASCADE_COUNT - 1;
  for (int i = (int)cascade_idx; i >= 0; --i) {
    // Select cascade based on whether or not the pixel is inside the projection
    float4x4 shadow_mat = l.light.cascade_vps[i];
    float4 proj_pos = mul(shadow_mat, float4(s.world_pos, 1.0f));
    float3 cascade_pos = proj_pos.xyz / proj_pos.w;
    cascade_pos.xy = cascade_pos.xy * 0.5 + 0.5;
    cascade_pos = abs(cascade_pos - 0.5f);
    if (all(cascade_pos < 0.5f)) {
      cascade_idx = i;
    }
  }

  const float3 cascade_colors[TB_CASCADE_COUNT] = {
      float3(1.0f, 0.0, 0.0f), float3(0.0f, 1.0f, 0.0f),
      float3(0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 0.0f)};

  // return cascade_colors[cascade_idx];

  float4x4 shadow_mat = l.light.cascade_vps[cascade_idx];
  float3 sample_pos = s.world_pos;
  float4 proj_pos = mul(shadow_mat, float4(sample_pos, 1.0f));
  float3 shadow_pos = proj_pos.xyz / proj_pos.w;
  shadow_pos.xy = shadow_pos.xy * 0.5 + 0.5;
  float3 shadow_pos_dx = ddx_fine(shadow_pos);
  float3 shadow_pos_dy = ddy_fine(shadow_pos);

  float visibility = sample_shadow_cascade(
      l.shadow_map, l.shadow_sampler, shadow_pos, shadow_pos_dx, shadow_pos_dy,
      shadow_mat, cascade_idx);

  // TODO: Sample across cascades?

  return visibility;
}

float3 pbr_lighting_common(View v, Light l, Surface s) {
  float3 out_color = 0;

  // Calculate shadow first
  float shadow = shadow_visibility(l, s);

  // Lighting
  {
    float3 L = l.light.light_dir;
    float3 albedo = s.base_color.rgb;
    float3 alpha = s.base_color.a;

    float2 brdf = v.brdf_lut
                      .Sample(v.brdf_sampler,
                              float2(max(dot(s.N, s.V), 0.0), s.roughness))
                      .rg;
    float3 reflection = prefiltered_reflection(
        v.prefiltered_map, v.filtered_env_sampler, s.R, s.roughness);
    float3 irradiance =
        v.irradiance_map.SampleLevel(v.filtered_env_sampler, s.N, 0).rgb;
    out_color =
        pbr_lighting(shadow, 1, albedo, s.metallic, s.roughness, brdf,
                     reflection, irradiance, l.light.color, L, s.V, s.N);
  }

  // Add emissive
  {
    float3 emissive_factor = s.emissives.rgb;
    float emissive_strength = s.emissives.w;
    out_color += emissive_factor * emissive_strength;
  }

  return out_color;
}
