#pragma once

#include "common.hlsli"

// Constant normal incidence Fresnel factor for all dielectrics.
static const float3 Fdielectric = 0.04;
static const float Epsilon = 0.00001;

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria https://archive.org/details/lambertsphotome00lambgoog
float3 diffuse(float3 color) {
  return color / PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
float3 specularReflection(float3 reflectance_0, float3 reflectance_90, float VdotH) {
  return reflectance_0 + (reflectance_90, - reflectance_0) * pow(clamp(1.0 - VdotH, 0, 1), 5);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
float geometricOcclusion(float NdotL, float NdotV, float alpha_roughness)
{
  float r = alpha_roughness;
  float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
  float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
  return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
float microfacetDistribution(float alpha_roughness, float NdotH)
{
  float roughnessSq = alpha_roughness * alpha_roughness;
  float f = (NdotH * roughnessSq - NdotH) * NdotH + 1.0;
  return roughnessSq / (PI * f * f);
}

// Uncharted2Tonemap From http://filmicgames.com/archives/75
float3 tonemap(float3 x)
{
  float A = 0.15;
  float B = 0.50;
  float C = 0.10;
  float D = 0.20;
  float E = 0.02;
  float F = 0.30;
  return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}