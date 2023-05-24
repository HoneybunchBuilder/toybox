#pragma once

#include "pbr.hlsli"

#define AMBIENT 0.7

float3 specular_contribution(float3 light_color, float3 albedo, float3 L,
                             float3 V, float3 N, float3 f0, float metallic,
                             float roughness) {
  float3 H = normalize(V + L);
  float NdotL = clamp(dot(N, L), 0.001, 1.0);
  float NdotH = clamp(dot(N, H), 0.001, 1.0);
  float NdotV = clamp(dot(N, V), 0.001, 1.0);

  float3 color = float3(0, 0, 0);

  if (NdotL > 0.0f) {
    float alpha = roughness * roughness;
    float D = microfacetDistribution(alpha, NdotH);
    float G = geometricOcclusion(NdotL, NdotV, roughness);
    float3 F = fresnesl_schlick(NdotV, f0);

    float3 spec = D * F * G / (4.0 * NdotL * NdotV + 0.001);
    float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
    color += (kD * albedo / PI + spec) * NdotL;
  }

  return color;
}

float3 pbr_lighting(float ao, float3 albedo, float metallic, float roughness, float2 brdf, float3 reflection, float3 irradiance, float3 light_color, float3 L, float3 V, float3 N) {
  float3 f0 = float3(0.04, 0.04, 0.04);
  f0 = lerp(f0, albedo, metallic);

  float3 direct = float3(0, 0, 0);
  // TODO: Handle more than one direct light
  {
    direct += specular_contribution(light_color, albedo, L, V, N, f0,
                                    metallic, roughness);
  }

  // Diffuse
  float3 diffuse = irradiance * albedo;

  // Specular
  float3 kS = fresnel_schlick_roughness(max(dot(N, V), 0.0f), f0, roughness);
  float3 specular = reflection * (kS * brdf.x + brdf.y);

  // Ambient
  float3 kD = 1.0 - kS;
  kD *= 1.0 - metallic;
  float3 ambient = (kD * diffuse + specular) * ao;

  return ambient + direct;
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

float texture_proj(float4 shadow_coord, float2 offset, float ambient,
                   Texture2D shadow_map, sampler samp) {
  float bias = 0.0005;

  float4 proj_coord = shadow_coord;

  proj_coord.xy = proj_coord.xy * 0.5 + 0.5;
  proj_coord = proj_coord / proj_coord.w;

  if(proj_coord.z <= -1.0 || shadow_coord.z >= 1.0) {
    return 1.0;
  }

  float sampled_depth =
      shadow_map.Sample(samp, float2(proj_coord.xy + offset)).r;

  return (proj_coord.w > 0 && sampled_depth < proj_coord.z - bias) ? saturate(1 - ambient)
                                                                   : 1.0f;
}

float pcf_filter(float4 shadow_coord, float ambient, Texture2D shadow_map,
                 sampler samp) {
  int2 tex_dim;
  shadow_map.GetDimensions(tex_dim.x, tex_dim.y);

  float scale = 0.75f;
  float dx = scale * (1.0 / float(tex_dim.x));
  float dy = scale * (1.0 / float(tex_dim.y));

  float shadow_factor = 0.0;
  uint count = 0;
  int range = 1;

  for (int x = -range; x <= range; ++x) {
    for (int y = -range; y <= range; ++y) {
      float2 offset = float2(dx * x, dy * y);
      shadow_factor += texture_proj(shadow_coord, offset, ambient, shadow_map,
                                    samp);
      count++;
    }
  }

  return shadow_factor / count;
}
