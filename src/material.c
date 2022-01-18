#include "material.h"

#include "gpuresources.h"
#include "pipelines.h"

#include <assert.h>

SubMaterialSelection phong_blinn_submaterial_select(materialoptionflags options,
                                                    const void *material) {
  const PhongBlinnMaterial *mat = (const PhongBlinnMaterial *)material;

  SubMaterialSelection selection = {0};

  if (options == MATOPT_None) {
    selection.submaterial_idx = 2;
  } else if (options == MATOPT_Alpha) {
    selection.submaterial_idx = 1;
  } else if (options == MATOPT_CastShadows) {
    selection.submaterial_idx = 0;
  } else {
    assert(false);
    selection.submaterial_idx = -1;
  }

  // bool has_albedo_map = mat->albedo_map != NULL; TODO
  bool has_normal_map = mat->normal_map != NULL;

  selection.pipeline_perm_flags = 0;
  if (has_normal_map) {
    selection.pipeline_perm_flags |= GLTF_PERM_NORMAL_MAP;
  }

  return selection;
}

PhongBlinnMaterial
phong_blinn_material_init(const PhongBlinnMaterialDesc *desc) {
  PhongBlinnMaterial mat = {
      .mat =
          {
              .submaterial_count = phong_blinn_submaterial_count,
              .submaterials =
                  {
                      // Shadow Caster
                      [0] =
                          {
                              .pass_count = 1,
                              .passes =
                                  {
                                      [0] = desc->shadowcast,
                                  },
                          },
                      // Transparent
                      [1] =
                          {
                              .pass_count = 2,
                              .passes =
                                  {
                                      [0] = desc->zprepassalpha,
                                      [1] = desc->coloralpha,
                                  },
                          },
                      // Opaque
                      [2] =
                          {
                              .pass_count = 2,
                              .passes =
                                  {
                                      [0] = desc->zprepassopaque,
                                      [1] = desc->coloropaque,
                                  },
                          },
                  },
              .submaterial_select = phong_blinn_submaterial_select,
          },
  };

  return mat;
}
