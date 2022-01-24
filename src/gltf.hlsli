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
  float base_color_factor[4];
  float metallic_factor;
  float roughness_factor;
} PBRMetallicRoughness;

typedef struct PBRSpecularGlossiness {
  float diffuse_factor[4];
  float specular_factor[3];
  float glossiness_factor;
} PBRSpecularGlossiness;

typedef struct Specular {
  float color_factor[3];
  float specular_factor;
} Specular;

typedef struct Sheen {
  float color_factor[3];
  float roughness_factor;
} Sheen;

typedef struct Volume {
  float thickness_factor;
  float attenuation_color[3];
  float attenuation_distance;
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
