#pragma once

#include "simd.h"

typedef struct GPUTexture GPUTexture;
typedef struct GPUPipeline GPUPipeline;
typedef struct GPUPass GPUPass;
typedef struct GPUConstBuffer GPUConstBuffer;

typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef enum materialoptionflags {
  MATOPT_None = 0x00000000,
  MATOPT_Alpha = 0x00000001,
  MATOPT_CastShadows = 0x00000002,
  MATOPT_Count = 3,
} materialoptionflags;

#define MAX_SUBMATERIALS (MATOPT_Count * MATOPT_Count)
#define MAX_PASS_PIPELINES 8

typedef void (*updatedescriptor_fn)(VkDescriptorSet, void *);

typedef struct SubMaterial {
  uint32_t pass_count;
  GPUPass *passes[MAX_PASS_PIPELINES];
  GPUConstBuffer *material_data[MAX_PASS_PIPELINES];
  GPUPipeline *pipelines[MAX_PASS_PIPELINES];
  updatedescriptor_fn update_descriptor_fns[MAX_PASS_PIPELINES];
} SubMaterial;

typedef struct SubMaterialSelection {
  int32_t submaterial_idx;
  uint32_t pipeline_perm_flags;
} SubMaterialSelection;

typedef SubMaterialSelection (*submaterialselect_fn)(materialoptionflags,
                                                     const void *);

typedef struct Material {
  uint32_t submaterial_count;
  SubMaterial submaterials[MAX_SUBMATERIALS];

  materialoptionflags options;
  submaterialselect_fn submaterial_select;
} Material;

typedef struct UnlitMaterial {
  float4 albedo;
  GPUTexture *albedo_map;

  GPUTexture *normal_map;
  Material mat;
} UnlitMaterial;

typedef struct PhongBlinnMaterial {
  float4 albedo;
  GPUTexture *albedo_map;

  GPUTexture *normal_map;

  Material mat;
} PhongBlinnMaterial;

static const uint32_t phong_blinn_submaterial_count = 3;
typedef struct PhongBlinnMaterialDesc {
  GPUPass *shadowcast;
  GPUPass *zprepassalpha;
  GPUPass *zprepassopaque;
  GPUPass *coloralpha;
  GPUPass *coloropaque;
} PhongBlinnMaterialDesc;

PhongBlinnMaterial
phong_blinn_material_init(const PhongBlinnMaterialDesc *desc);

typedef struct MetalRoughMaterial {
  float4 albedo;
  GPUTexture *albedo_map;

  GPUTexture *normal_map;

  float metallic;
  GPUTexture *metallic_map;

  float roughness;
  GPUTexture *roughness_map;

  Material mat;
} MetalRoughMaterial;
