#pragma once

#include "common.hlsli"

#define GLTF_PERM_BASE_COLOR_MAP 0x00000001
#define GLTF_PERM_NORMAL_MAP 0x00000002
#define GLTF_PERM_PBR_METALLIC_ROUGHNESS 0x00000004
#define GLTF_PERM_PBR_METAL_ROUGH_TEX 0x00000008
#define GLTF_PERM_PBR_SPECULAR_GLOSSINESS 0x00000010
#define GLTF_PERM_CLEARCOAT 0x00000020
#define GLTF_PERM_TRANSMISSION 0x00000040
#define GLTF_PERM_VOLUME 0x00000080
#define GLTF_PERM_IOR 0x00000100
#define GLTF_PERM_SPECULAR 0x00000200
#define GLTF_PERM_SHEEN 0x000000400
#define GLTF_PERM_UNLIT 0x00000800

#ifdef __HLSL_VERSION
#define PACKED
#define PADDING(t, n) t n;
#else
#define PACKED __attribute__((__packed__))
#define PADDING(t, n)
#endif

// Omitting texture rotation because it's not widely used
typedef struct PACKED TextureTransform {
  float2 offset;
  float2 scale;
} TextureTransform;

typedef struct PACKED PBRMetallicRoughness {
  float4 base_color_factor;
  float4 metal_rough_factors;
} PBRMetallicRoughness;

typedef struct PACKED PBRSpecularGlossiness {
  float4 diffuse_factor;
  float4 spec_gloss_factors;
} PBRSpecularGlossiness;

typedef struct PACKED GLTFMaterialData {
  TextureTransform tex_transform;
  PBRMetallicRoughness pbr_metallic_roughness;
  PBRSpecularGlossiness pbr_specular_glossiness;
  float4 specular;
  float4 sheen;
  float4 attenuation_params;
  float4 thickness_factor;
  float4 emissives;
} GLTFMaterialData;

typedef struct PACKED MaterialPushConstants {
  uint perm;
} MaterialPushConstants;

// If a shader, provide some helper functions
#ifdef __HLSL_VERSION
float2 uv_transform(int2 quant_uv, TextureTransform trans) {
  // Must dequantize UV from integer to float before applying the transform
  float2 uv = float2(quant_uv) / 65535.0f;
  uv *= trans.scale;
  uv += trans.offset;
  return uv;
}
#else
_Static_assert(sizeof(MaterialPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
#endif
