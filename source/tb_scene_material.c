#include "tb_scene_material.h"

#include "gltf.hlsli"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_texture_system.h"

typedef struct TbSceneMaterial {
  const cgltf_data *gltf_data;
  const char *name;
  TbGLTFMaterialData data;
  TbTexture color_map;
  TbTexture normal_map;
  TbTexture metal_rough_map;
} TbSceneMaterial;

// Run on a thread
bool tb_parse_scene_mat(const cgltf_data *gltf_data, const char *name,
                        const cgltf_material *material, void **out_mat_data) {
  *out_mat_data = tb_alloc(tb_global_alloc, sizeof(TbSceneMaterial));
  tb_auto scene_mat = (TbSceneMaterial *)*out_mat_data;

  const size_t name_len = SDL_strnlen(name, 256) + 1;
  char *name_cpy = tb_alloc_nm_tp(tb_global_alloc, name_len, char);
  SDL_strlcpy(name_cpy, name, name_len);

  scene_mat->gltf_data = gltf_data;
  scene_mat->name = name_cpy;

  // Find a suitable texture transform from the material
  cgltf_texture_transform tex_trans = {
      .scale = {1, 1},
  };
  if (material) {
    // Expecting that all textures in the material share the same texture
    // transform
    if (material->has_pbr_metallic_roughness) {
      tex_trans = material->pbr_metallic_roughness.base_color_texture.transform;
    } else if (material->has_pbr_specular_glossiness) {
      tex_trans = material->pbr_specular_glossiness.diffuse_texture.transform;
    } else if (material->normal_texture.texture) {
      tex_trans = material->normal_texture.transform;
    }
  }

  TbMaterialPerm feat_perm = 0;
  if (material->has_pbr_metallic_roughness) {
    feat_perm |= GLTF_PERM_PBR_METALLIC_ROUGHNESS;
    if (material->pbr_metallic_roughness.metallic_roughness_texture.texture !=
        NULL) {
      feat_perm |= GLTF_PERM_PBR_METAL_ROUGH_TEX;
    }
    if (material->pbr_metallic_roughness.base_color_texture.texture != NULL) {
      feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
    }
  }
  if (material->has_pbr_specular_glossiness) {
    feat_perm |= GLTF_PERM_PBR_SPECULAR_GLOSSINESS;

    if (material->pbr_specular_glossiness.diffuse_texture.texture != NULL &&
        material->pbr_specular_glossiness.specular_glossiness_texture.texture !=
            NULL) {
      // feat_perm |= GLTF_PERM_PBR_SPECULAR_GLOSSINESS_TEX;
    }
    if (material->pbr_specular_glossiness.diffuse_texture.texture != NULL) {
      feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
    }
  }
  if (material->has_clearcoat) {
    feat_perm |= GLTF_PERM_CLEARCOAT;
  }
  if (material->has_transmission) {
    feat_perm |= GLTF_PERM_TRANSMISSION;
  }
  if (material->has_volume) {
    feat_perm |= GLTF_PERM_VOLUME;
  }
  if (material->has_ior) {
    feat_perm |= GLTF_PERM_IOR;
  }
  if (material->has_specular) {
    feat_perm |= GLTF_PERM_SPECULAR;
  }
  if (material->has_sheen) {
    feat_perm |= GLTF_PERM_SHEEN;
  }
  if (material->unlit) {
    feat_perm |= GLTF_PERM_UNLIT;
  }
  if (material->alpha_mode == cgltf_alpha_mode_mask) {
    feat_perm |= GLTF_PERM_ALPHA_CLIP;
  }
  if (material->alpha_mode == cgltf_alpha_mode_blend) {
    feat_perm |= GLTF_PERM_ALPHA_BLEND;
  }
  if (material->double_sided) {
    feat_perm |= GLTF_PERM_DOUBLE_SIDED;
  }
  if (material->normal_texture.texture != NULL) {
    feat_perm |= GLTF_PERM_NORMAL_MAP;
  }

  scene_mat->data = (TbGLTFMaterialData){
      .tex_transform =
          {
              .offset =
                  (float2){
                      tex_trans.offset[0],
                      tex_trans.offset[1],
                  },
              .scale =
                  (float2){
                      tex_trans.scale[0],
                      tex_trans.scale[1],
                  },
          },
      .pbr_metallic_roughness =
          {
              .base_color_factor =
                  tb_atof4(material->pbr_metallic_roughness.base_color_factor),
              .metal_rough_factors =
                  {material->pbr_metallic_roughness.metallic_factor,
                   material->pbr_metallic_roughness.roughness_factor},
          },
      .pbr_specular_glossiness.diffuse_factor =
          tb_atof4(material->pbr_specular_glossiness.diffuse_factor),
      .specular =
          tb_f3tof4(tb_atof3(material->pbr_specular_glossiness.specular_factor),
                    material->pbr_specular_glossiness.glossiness_factor),
      .emissives = tb_f3tof4(tb_atof3(material->emissive_factor), 1.0f),
      .perm = feat_perm,
  };

  if (material->has_emissive_strength) {
    scene_mat->data.emissives[3] =
        material->emissive_strength.emissive_strength;
  }
  if (material->alpha_mode == cgltf_alpha_mode_mask) {
    scene_mat->data.sheen_alpha[3] = material->alpha_cutoff;
  }

  return true;
}

// Run in a task on the main thread
void tb_load_scene_mat(ecs_world_t *ecs, void *mat_data) {
  tb_auto scene_mat = (TbSceneMaterial *)mat_data;
  if (scene_mat == NULL) {
    return;
  }

  const cgltf_data *gltf_data = scene_mat->gltf_data;
  const char *name = scene_mat->name;

  tb_auto color = tb_get_default_color_tex(ecs);
  if ((scene_mat->data.perm & GLTF_PERM_BASE_COLOR_MAP) > 0) {
    color = tb_tex_sys_load_mat_tex(ecs, gltf_data, name, TB_TEX_USAGE_COLOR);
  }
  scene_mat->color_map = color;

  tb_auto normal = tb_get_default_normal_tex(ecs);
  if ((scene_mat->data.perm & GLTF_PERM_NORMAL_MAP) > 0) {
    normal = tb_tex_sys_load_mat_tex(ecs, gltf_data, name, TB_TEX_USAGE_NORMAL);
  }
  scene_mat->normal_map = normal;

  tb_auto metal_rough = tb_get_default_metal_rough_tex(ecs);
  if ((scene_mat->data.perm & GLTF_PERM_PBR_METAL_ROUGH_TEX) > 0) {
    metal_rough =
        tb_tex_sys_load_mat_tex(ecs, gltf_data, name, TB_TEX_USAGE_METAL_ROUGH);
  }
  scene_mat->metal_rough_map = metal_rough;

  tb_free(tb_global_alloc, (void *)name);
}

bool tb_is_scene_mat_ready(ecs_world_t *ecs, const TbMaterialData *data) {
  tb_auto scene_mat = (TbSceneMaterial *)data->domain_data;
  if (scene_mat == NULL) {
    return false;
  }

  return tb_is_texture_ready(ecs, scene_mat->color_map) &&
         tb_is_texture_ready(ecs, scene_mat->normal_map) &&
         tb_is_texture_ready(ecs, scene_mat->metal_rough_map);
}

void *tb_get_scene_mat_data(ecs_world_t *ecs, const TbMaterialData *data) {
  tb_auto scene_mat = (TbSceneMaterial *)data->domain_data;
  scene_mat->data.color_idx =
      *ecs_get(ecs, scene_mat->color_map, TbTextureComponent);
  scene_mat->data.normal_idx =
      *ecs_get(ecs, scene_mat->normal_map, TbTextureComponent);
  scene_mat->data.pbr_idx =
      *ecs_get(ecs, scene_mat->metal_rough_map, TbTextureComponent);
  return (void *)&scene_mat->data;
}

size_t tb_get_scene_mat_size(void) { return sizeof(TbGLTFMaterialData); }

bool tb_is_scene_mat_trans(const TbMaterialData *data) {
  tb_auto scene_mat = (TbSceneMaterial *)data->domain_data;
  tb_auto perm = scene_mat->data.perm;
  return perm & GLTF_PERM_ALPHA_CLIP || perm & GLTF_PERM_ALPHA_BLEND;
}

void tb_register_scene_material_domain(ecs_world_t *ecs) {
  TbSceneMaterial default_scene_mat = {
      .data =
          {
              .perm = GLTF_PERM_BASE_COLOR_MAP | GLTF_PERM_NORMAL_MAP |
                      GLTF_PERM_PBR_METAL_ROUGH_TEX,
          },
      .color_map = tb_get_default_color_tex(ecs),
      .metal_rough_map = tb_get_default_metal_rough_tex(ecs),
      .normal_map = tb_get_default_normal_tex(ecs),
  };

  TbMaterialDomain domain = {
      .parse_fn = tb_parse_scene_mat,
      .load_fn = tb_load_scene_mat,
      .ready_fn = tb_is_scene_mat_ready,
      .get_data_fn = tb_get_scene_mat_data,
      .get_size_fn = tb_get_scene_mat_size,
      .is_trans_fn = tb_is_scene_mat_trans,
  };

  tb_register_mat_usage(ecs, "scene", TB_MAT_USAGE_SCENE, domain,
                        &default_scene_mat, sizeof(TbSceneMaterial));
}
