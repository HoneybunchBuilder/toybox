#pragma once

#include "pbr.hlsli"

float3 pbr_lighting(float ao, float3 albedo, float metallic, float roughness,
                    float2 brdf, float3 reflection, float3 irradiance,
                    float3 light_color, float3 L, float3 V, float3 N) {
  float3 F0 = lerp(0.04, albedo, metallic);

  float3 Lo = 0;
  // Calculate direct lighting
  // TODO: Should be from multiple sources but for now this is all for one light
  {
    float3 H = normalize(V + L);
    float distance = 1.0f; // TODO: account for attenuation
    float attenuation = 1.0 / (distance * distance);
    float3 radiance = light_color * attenuation;

    float NdotH = max(dot(N, H), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);

    // Cook-Torrance BRDF
    float NDF = microfacetDistribution(roughness, NdotH);
    float G = geometricOcclusion(NdotL, NdotV, roughness);
    float3 F = fresnesl_schlick(max(dot(H, V), 0), F0);

    float3 numerator = NDF * G * F;
    // + 0.0001 to prevent divide by zero
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    Lo += (kD * albedo / PI + specular) * radiance * NdotL;
  }

  // Calculate ambient light
  float3 F = fresnel_schlick_roughness(max(dot(N, V), 0.0), F0, roughness);
  float3 kS = F;
  float3 kD = (1.0 - kS) * (1.0 - metallic);

  float3 diffuse = irradiance * albedo;
  // Reflection is assumed to be pre-filtered by the caller
  float3 specular = reflection * (F * brdf.x + brdf.y);

  float3 ambient = (kD * diffuse + specular) * ao;

  return ambient + Lo;
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
