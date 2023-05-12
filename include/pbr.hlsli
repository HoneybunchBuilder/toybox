#pragma once

#ifndef TB_PBR_H
#define TB_PBR_H

#include "common.hlsli"

// Constant normal incidence Fresnel factor for all dielectrics.
static const float3 Fdielectric = 0.04;
static const float Epsilon = 0.00001;

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria
// https://archive.org/details/lambertsphotome00lambgoog
float3 diffuse(float3 color) { return color / PI; }

// The following equation models the Fresnel reflectance term of the spec
// equation (aka F())
float3 specularReflection(float3 reflectance_0, float3 reflectance_90,
                          float VdotH) {
  return reflectance_0 +
         (reflectance_90, -reflectance_0) * pow(clamp(1.0 - VdotH, 0, 1), 5);
}

float3 fresnesl_schlick(float cos_theta, float3 f0){
  return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

float3 fresnel_schlick_roughness(float cos_theta, float3 F0, float roughness) {
  return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(1.0 - cos_theta, 5.0);
}

float3 prefiltered_reflection(TextureCube map, SamplerState s,  float3 R, float roughness) {
  const float MAX_REFLECTION_LOD = 9.0; // todo: param/const
  float lod = roughness * MAX_REFLECTION_LOD;
  float lodf = floor(lod);
  float lodc = ceil(lod);
  float3 a = map.SampleLevel(s, R, lodf).rgb;
  float3 b = map.SampleLevel(s, R, lodc).rgb;
  return lerp(a, b, lod - lodf);
}


// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
float geometricOcclusion(float NdotL, float NdotV, float roughness) {
  float r = roughness + 1;
  float k = (r * r) / 8.0;
  float GL = NdotL / (NdotL * (1.0 - k) + k);
  float GV = NdotV / (NdotV * (1.0 - k) + k);
  return GL * GV;
}

// The following equation(s) model the distribution of microfacet normals across
// the area being drawn (aka D()) Implementation from "Average Irregularity
// Representation of a Roughened Surface for Ray Reflection" by T. S.
// Trowbridge, and K. P. Reitz
float microfacetDistribution(float alpha_roughness, float NdotH) {
  float roughnessSq = alpha_roughness * alpha_roughness;
  float denom = NdotH * NdotH * (roughnessSq - 1.0) + 1.0;
  return roughnessSq / (PI * denom * denom);
}

#endif
