#pragma once

#include "pbr.hlsli"

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
                   float2 brdf, float3 albedo, float metallic,
                   float roughness) {
  float3 F = fresnel_schlick_roughness(NdotV, F0, roughness);

  float3 kd = lerp(1.0f - F, 0.0f, metallic);

  float3 diffuse_ibl = kd * albedo * irradiance;

  float3 specular_irradiance = reflection;
  float3 specular_ibl = (F0 * brdf.x + brdf.y) * reflection;

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
                               metallic, roughness) *
                   ao;
  float3 direct = 0;
  if (shadow >= 0.3f) {
    direct = pbr_direct(NdotV, F0, N, V, L, light_color, albedo, metallic,
                        roughness);
  }

  ambient *= shadow;

  return direct + ambient;
}

float3 phong_light(float3 albedo, float3 light_color, float gloss, float3 N,
                   float3 L, float3 V, float3 H) {
  // Calc diffuse Light
  float lambert = saturate(dot(N, L));
  float3 diffuse = light_color * lambert * albedo;

  // Calc specular light
  float3 specular_exponent = exp2(gloss * 11) + 2;
  float3 specular = saturate(dot(H, N)) * (lambert > 0); // Blinn-Phong
  specular = pow(specular, specular_exponent) * gloss;
  specular *= light_color;

  float3 color = diffuse + specular;
  return color;
}

// Shadowing
#define AMBIENT 0.7
float texture_proj(float4 shadow_coord, float2 offset, uint cascade_idx,
                   Texture2D shadow_map, sampler samp) {
  float bias = 0.0005;

  float4 proj_coord = shadow_coord;

  proj_coord.xy = proj_coord.xy * 0.5 + 0.5;
  proj_coord.y /= (float)TB_CASCADE_COUNT;
  proj_coord.y += ((float)cascade_idx / (float)TB_CASCADE_COUNT);
  proj_coord = proj_coord / proj_coord.w;

  if (proj_coord.z <= -1.0 || shadow_coord.z >= 1.0) {
    return 1.0;
  }

  float sampled_depth =
      shadow_map.Sample(samp, float2(proj_coord.xy + offset)).r;

  return (proj_coord.w > 0 && sampled_depth < proj_coord.z - bias)
             ? saturate(1 - AMBIENT)
             : 1.0f;
}

float pcf_filter(float4 shadow_coord, Texture2D shadow_map, uint cascade_idx,
                 sampler samp) {
  int2 tex_dim;
  shadow_map.GetDimensions(tex_dim.x, tex_dim.y);

  float scale = 0.5f;
  float dx = scale * (1.0 / float(tex_dim.x));
  float dy = scale * (1.0 / float(tex_dim.y));

  float shadow_factor = 0.0;
  uint count = 0;
  int range = 1;

  for (int x = -range; x <= range; ++x) {
    for (int y = -range; y <= range; ++y) {
      float2 offset = float2(dx * x, dy * y);
      shadow_factor +=
          texture_proj(shadow_coord, offset, cascade_idx, shadow_map, samp);
      count++;
    }
  }

  return shadow_factor / count;
}

struct View {
  TextureCube irradiance_map;
  TextureCube prefiltered_map;
  Texture2D brdf_lut;
  Texture2D ssao_map;
  sampler filtered_env_sampler;
  sampler brdf_sampler;
};

struct Light {
  CommonLightData light;
  Texture2D shadow_map;
  sampler shadow_sampler;
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

float3 pbr_lighting_common(View v, Light l, Surface s) {
  float3 out_color = 0;

  // Calculate shadow first
  float shadow = 1.0f; // A value of 0 means the pixel is completely lit
  {
    uint cascade_idx = 0;
    for (uint c = 0; c < (TB_CASCADE_COUNT - 1); ++c) {
      if (s.view_pos.z < l.light.cascade_splits[c]) {
        cascade_idx = c + 1;
      }
    }

    float4 shadow_coord =
        mul(l.light.cascade_vps[cascade_idx], float4(s.world_pos, 1.0));

    shadow =
        pcf_filter(shadow_coord, l.shadow_map, cascade_idx, l.shadow_sampler);
  }

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
    float ao = v.ssao_map.Sample(l.shadow_sampler, s.screen_uv).r;
    out_color =
        pbr_lighting(shadow, ao, albedo, s.metallic, s.roughness, brdf,
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
