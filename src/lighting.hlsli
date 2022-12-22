#pragma once

#include "pbr.hlsli"

#define AMBIENT 0.1

typedef struct PBRLight {
  float3 color;
  float3 L;
  float3 reflectance_0;
  float3 reflectance_90;
  float alpha_roughness;
  float3 diffuse_color;
} PBRLight;

float3 pbr_lighting(PBRLight light, float3 N, float3 V, float NdotV)
{
  float3 L = light.L;

  // Per light calcs
  float3 H = normalize(V + L);

  float NdotL = clamp(dot(N, L), 0.001, 1.0);
  float NdotH = clamp(dot(N, H), 0.001, 1.0);
  float LdotH = clamp(dot(L, H), 0.001, 1.0);
  float VdotH = clamp(dot(V, H), 0.001, 1.0);

  float3 F = specularReflection(light.reflectance_0, light.reflectance_90, VdotH);
  float G = geometricOcclusion(NdotL, NdotV, light.alpha_roughness);
  float D = microfacetDistribution(light.alpha_roughness, NdotH);

  // Calculation of analytical lighting contribution
  float3 diffuse_contrib = (1.0 - F) * diffuse(light.diffuse_color);
  float3 specular_contrib = F * G * D / (4.0 * NdotL * NdotV);
  // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
  return NdotL * light.color * (diffuse_contrib + specular_contrib);
}

float3 phong_light(float3 albedo, float3 light_color, float gloss, float3 N, float3 L, float3 V, float3 H)
{
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

float texture_proj(float4 shadow_coord, float2 offset, float ambient, Texture2D shadow_maps[4], sampler samp, float NdotL, uint cascade_idx)
{
  float bias = max(0.005 * (1.0 - NdotL), 0.0005);

  float4 proj_coord = shadow_coord;

  proj_coord.xy = proj_coord.xy * 0.5 + 0.5;
  proj_coord = proj_coord / proj_coord.w;

  float sampled_depth = shadow_maps[cascade_idx].Sample(samp, float2(proj_coord.xy + offset)).r;

  return (proj_coord.w > 0 && sampled_depth < proj_coord.z - bias) ? ambient : 1.0f;
}

float pcf_filter(float4 shadow_coord, float ambient, Texture2D shadow_maps[4], sampler samp, float NdotL, uint cascade_idx)
{
  int2 tex_dim;
  shadow_maps[cascade_idx].GetDimensions(tex_dim.x, tex_dim.y);

  float scale = 0.75f;
  float dx = scale * (1.0 / float(tex_dim.x));
  float dy = scale * (1.0 / float(tex_dim.y));

  float shadow_factor = 0.0;
  uint count = 0;
  int range = 1;

  for(int x = -range; x <= range; ++x)
  {
    for(int y = -range; y <= range; ++y)
    {
      float2 offset = float2(dx * x, dy * y);
      shadow_factor += texture_proj(shadow_coord, offset, ambient, shadow_maps, samp, NdotL, cascade_idx);
      count++;
    }
  }

  return shadow_factor / count;
}
