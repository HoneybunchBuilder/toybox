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
#define GLTF_PERM_ALPHA_CLIP 0x00001000
#define GLTF_PERM_ALPHA_BLEND 0x00002000
#define GLTF_PERM_DOUBLE_SIDED 0x00004000

#ifdef TB_SHADER

// Omitting texture rotation because it's not widely used
struct TextureTransform {
  float2 offset;
  float2 scale;
};

struct PBRMetallicRoughness {
  float4 base_color_factor;
  float4 metal_rough_factors;
};

struct PBRSpecularGlossiness {
  float4 diffuse_factor;
  float4 spec_gloss_factors;
};

struct TbGLTFMaterialData {
  TextureTransform tex_transform;
  PBRMetallicRoughness pbr_metallic_roughness;
  PBRSpecularGlossiness pbr_specular_glossiness;
  float4 specular;
  float4 sheen_alpha; // alpha cutoff packed into w
  float4 attenuation_params;
  float4 thickness_factor;
  float4 emissives;
  int32_t perm;
  uint32_t color_idx;
  uint32_t normal_idx;
  uint32_t pbr_idx;
};

// Per-draw lookup table
struct TbGLTFDrawData {
  int32_t perm; // Input layout permutation
  uint32_t obj_idx;
  uint32_t mesh_idx;
  uint32_t mat_idx;
  uint32_t index_offset;
  uint32_t vertex_offset;
  uint32_t pad0;
  uint32_t pad1;
};

#else

// Omitting texture rotation because it's not widely used
typedef struct TB_GPU_STRUCT TextureTransform {
  float2 offset;
  float2 scale;
} TextureTransform;

typedef struct TB_GPU_STRUCT PBRMetallicRoughness {
  float4 base_color_factor;
  float4 metal_rough_factors;
} PBRMetallicRoughness;

typedef struct TB_GPU_STRUCT PBRSpecularGlossiness {
  float4 diffuse_factor;
  float4 spec_gloss_factors;
} PBRSpecularGlossiness;

typedef struct TB_GPU_STRUCT TbGLTFMaterialData {
  TextureTransform tex_transform;
  PBRMetallicRoughness pbr_metallic_roughness;
  PBRSpecularGlossiness pbr_specular_glossiness;
  float4 specular;
  float4 sheen_alpha; // alpha cutoff packed into w
  float4 attenuation_params;
  float4 thickness_factor;
  float4 emissives;
  int32_t perm;
  uint32_t color_idx;
  uint32_t normal_idx;
  uint32_t pbr_idx;
} TbGLTFMaterialData;

// Per-draw lookup table
typedef struct TB_GPU_STRUCT TbGLTFDrawData {
  int32_t perm; // Input layout permutation
  uint32_t obj_idx;
  uint32_t mesh_idx;
  uint32_t mat_idx;
  uint32_t index_offset;
  uint32_t vertex_offset;
  uint32_t pad0;
  uint32_t pad1;
} TbGLTFDrawData;

#endif

#define ALPHA_CUTOFF(m) m.sheen_alpha.w

#if defined(TB_SHADER) && !defined(TB_SHADER)
#define TB_SHADER 1
#endif

// If a shader, provide some helper functions and macros
#if defined(TB_SHADER)
float2 uv_transform(int2 quant_uv, TextureTransform trans) {
  // Must dequantize UV from integer to float before applying the transform
  float2 uv = float2(quant_uv) / 65535.0f;
  uv *= trans.scale;
  uv += trans.offset;
  return uv;
}

// Macros for declaring access to GLTF specific descriptor sets managed
// by specific systems

#define GLTF_MATERIAL_SET(b)                                                   \
  [[vk::binding(0, b)]] SamplerState material_sampler;                         \
  [[vk::binding(1, b)]] SamplerComparisonState shadow_sampler;                 \
  [[vk::binding(2, b)]] StructuredBuffer<TbGLTFMaterialData> gltf_data[];

#define GLTF_DRAW_SET(b)                                                       \
  [[vk::binding(0, b)]] StructuredBuffer<TbGLTFDrawData> draw_data;

#define GLTF_VIEW_SET(b)                                                       \
  [[vk::binding(0, b)]] ConstantBuffer<TbCommonViewData> camera_data;          \
  [[vk::binding(1, b)]] TextureCube irradiance_map;                            \
  [[vk::binding(2, b)]] TextureCube prefiltered_map;                           \
  [[vk::binding(3, b)]] Texture2D brdf_lut;                                    \
  [[vk::binding(4, b)]] ConstantBuffer<TbCommonLightData> light_data;          \
  [[vk::binding(5, b)]] Texture2DArray shadow_map;                             \
  [[vk::binding(6, b)]] SamplerState filtered_env_sampler;                     \
  [[vk::binding(7, b)]] SamplerState brdf_sampler;

#define GLTF_OPAQUE_LIGHTING(out, color, normal, view, refl, s_uv, met, rough) \
  {                                                                            \
    TbView v;                                                                  \
    v.irradiance_map = irradiance_map;                                         \
    v.prefiltered_map = prefiltered_map;                                       \
    v.brdf_lut = brdf_lut;                                                     \
    v.filtered_env_sampler = filtered_env_sampler;                             \
    v.brdf_sampler = brdf_sampler;                                             \
                                                                               \
    Light l;                                                                   \
    l.light = light_data;                                                      \
    l.shadow_map = shadow_map;                                                 \
    l.shadow_sampler = shadow_sampler;                                         \
                                                                               \
    Surface s;                                                                 \
    s.base_color = color;                                                      \
    s.view_pos = i.view_pos;                                                   \
    s.world_pos = i.world_pos;                                                 \
    s.screen_uv = s_uv;                                                        \
    s.metallic = met;                                                          \
    s.roughness = rough;                                                       \
    s.N = normal;                                                              \
    s.V = view;                                                                \
    s.R = refl;                                                                \
    s.emissives = gltf.emissives;                                              \
                                                                               \
    out.rgb = pbr_lighting_common(v, l, s);                                    \
    out.a = color.a;                                                           \
  }

TbGLTFDrawData tb_get_gltf_draw_data(int32_t draw,
                                     StructuredBuffer<TbGLTFDrawData> data) {
  return data[draw];
}

TbGLTFMaterialData
tb_get_gltf_mat_data(int32_t mat,
                     StructuredBuffer<TbGLTFMaterialData> buffers[]) {
  return buffers[NonUniformResourceIndex(mat)][0];
}

#endif
