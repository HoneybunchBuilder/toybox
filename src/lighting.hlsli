#pragma once

#include "pbr.hlsli"

float3 pbr_metal_rough_light(float3 F0, float3 albedo, float3 light_color, float metallic, float roughness, float3 N, float3 L, float3 V, float3 H)
{
  // For point lights
  // float distance = length(light_data.light_pos - i.world_pos);
  float distance = 1.0;
  float attenuation = 1.0 / (distance * distance);
  float3 radiance = light_color * attenuation;

  // cook-torrance brdf
  float NDF = distributionGGX(N, H, roughness);
  float G = geometrySmith(N, V, L, roughness);
  float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

  float3 kS = F;
  float3 kD = float3(1.0, 1.0, 1.0) - kS;
  kD *= 1.0 - metallic;

  float3 numerator = NDF * G * F;
  float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.00001;
  float3 specular = numerator / denominator;

  float NdotL = max(dot(N, L), 0.0);
  float3 color = (kD * albedo / PI + specular) * radiance * NdotL;
  return color;
}

float3 phong_light(float3 albedo, float3 light_color, float gloss, float3 N, float3 L, float3 V, float3 H)
{
  // Calc ambient light
  float3 ambient = float3(0.03, 0.03, 0.03) * albedo;

  // Calc diffuse Light
  float lambert = saturate(dot(N, L));
  float3 diffuse = light_color * lambert * albedo;

  // Calc specular light
  float3 specular_exponent = exp2(gloss * 11) + 2;
  float3 specular = saturate(dot(H, N)) * (lambert > 0); // Blinn-Phong
  specular = pow(specular, specular_exponent) * gloss;
  specular *= light_color;

  float3 color = ambient + diffuse + specular;
  return color;
}