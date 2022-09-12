#include "materialsystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "rendersystem.h"
#include "world.h"

bool create_material_system(MaterialSystem *self,
                            const MaterialSystemDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  // Find the render system
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which materials depends on",
                  VK_ERROR_UNKNOWN);

  *self = (MaterialSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  return true;
}

void destroy_material_system(MaterialSystem *self) {
  RenderSystem *render_system = self->render_system;
  (void)render_system;

  *self = (MaterialSystem){0};
}

void tick_material_system(MaterialSystem *self, const SystemInput *input,
                          SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(material, MaterialSystem, MaterialSystemDescriptor)

void tb_material_system_descriptor(SystemDescriptor *desc,
                                   const MaterialSystemDescriptor *mat_desc) {
  desc->name = "Material";
  desc->size = sizeof(MaterialSystem);
  desc->id = MaterialSystemId;
  desc->desc = (InternalDescriptor)mat_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 0;
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_material_system;
  desc->destroy = tb_destroy_material_system;
  desc->tick = tb_tick_material_system;
}

TbMaterialId tb_mat_system_load_material(MaterialSystem *self, const char *path,
                                         const cgltf_material *material) {
  return InvalidMaterialId;
}
