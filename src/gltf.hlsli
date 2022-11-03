#pragma once

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

// Omitting texture rotation because it's not widely used
typedef struct TextureTransform {
  float2 offset;
  float2 scale;
} TextureTransform;

typedef struct PBRMetallicRoughness {
  float4 base_color_factor;
  float metallic_factor;
  float roughness_factor;
} PBRMetallicRoughness;

typedef struct PBRSpecularGlossiness {
  float4 diffuse_factor;
  float3 specular_factor;
  float glossiness_factor;
} PBRSpecularGlossiness;

typedef struct Specular {
  float3 color_factor;
  float specular_factor;
} Specular;

typedef struct Sheen {
  float3 color_factor;
  float roughness_factor;
} Sheen;

typedef struct Volume {
  float3 attenuation_color;
  float attenuation_distance;
  float thickness_factor;
} Volume;

typedef struct GLTFMaterialData {
  TextureTransform tex_transform;
  PBRMetallicRoughness pbr_metallic_roughness;
  PBRSpecularGlossiness pbr_specular_glossiness;
  float clearcoat_factor;
  float clearcoat_roughness_factor;
  float ior;
  Specular specular;
  Sheen sheen;
  float transmission_factor;
  Volume volume;
} GLTFMaterialData;

// If a shader, provide some helper functions
#ifdef __HLSL_VERSION
float2 uv_transform(int2 quant_uv, TextureTransform trans) {
  // Must dequantize UV from integer to float before applying the transform
  float2 uv = float2(quant_uv) / 65535.0f;
  uv += trans.offset;
  uv *= trans.scale;
  return uv;
}
#endif
