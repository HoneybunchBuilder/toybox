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

float3 pbr_light(float3 F0, float3 light_color, float3 albedo, float metallic, float roughness, float3 N, float3 L, float3 V, float cosLo)
{
  float3 Lh = normalize(L + V);
  
  // Calculate angles between surface normal and various light vectors.
  float cosLi = max(0.0, dot(N, L));
  float cosLh = max(0.0, dot(N, Lh));

  // Calculate Fresnel term for direct lighting. 
  float3 F  = fresnelSchlick(max(0.0, dot(Lh, V)), F0);
  // Calculate normal distribution for specular BRDF.
  float D = ndfGGX(cosLh, roughness);
  // Calculate geometric attenuation for specular BRDF.
  float G = gaSchlickGGX(cosLi, cosLo, roughness);

  // Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
  // Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
  // To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
  float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metallic);

  // Lambert diffuse BRDF.
  // We don't scale by 1/PI for lighting & material units to be more convenient.
  // See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
  float3 diffuseBRDF = kd * albedo;

  // Cook-Torrance specular microfacet BRDF.
  float3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

  // Total contribution for this light.
  return (diffuseBRDF + specularBRDF) * light_color * cosLi;
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

float texture_proj(float4 shadow_coord, float2 off, float ambient, Texture2D shadow_map, sampler samp)
{
  float3 proj_coords = shadow_coord.xyz / shadow_coord.w;
  proj_coords = proj_coords * 0.5 + 0.5;
  proj_coords.y = -proj_coords.y;
  float closest_depth = shadow_map.Sample( samp, proj_coords.xy ).r;
  float current_depth = shadow_coord.z / shadow_coord.w;
  return current_depth > closest_depth ? 1.0 : ambient;
}