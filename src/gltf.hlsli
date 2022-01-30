#pragma once

#define GLTF_PERM_NORMAL_MAP 0x00000001
#define GLTF_PERM_PBR_METALLIC_ROUGHNESS 0x00000002
#define GLTF_PERM_PBR_METAL_ROUGH_TEX 0x00000004
#define GLTF_PERM_PBR_SPECULAR_GLOSSINESS 0x00000008
#define GLTF_PERM_CLEARCOAT 0x00000010
#define GLTF_PERM_TRANSMISSION 0x00000020
#define GLTF_PERM_VOLUME 0x00000040
#define GLTF_PERM_IOR 0x00000080
#define GLTF_PERM_SPECULAR 0x00000100
#define GLTF_PERM_SHEEN 0x000000200
#define GLTF_PERM_UNLIT 0x00000400

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
