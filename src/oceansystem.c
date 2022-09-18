#include "oceansystem.h"

#include "assets.h"
#include "cgltf.h"
#include "meshsystem.h"
#include "oceancomponent.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

bool create_ocean_system(OceanSystem *self, const OceanSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which ocean depends on", false);
  MeshSystem *mesh_system = (MeshSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, MeshSystemId);
  TB_CHECK_RETURN(mesh_system,
                  "Failed to find mesh system which ocean depends on", false);

  *self = (OceanSystem){
      .render_system = render_system,
      .mesh_system = mesh_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Load the known glb that has the ocean mesh
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(self->tmp_alloc, "scenes/Ocean.glb");

  // Load glb off disk
  cgltf_data *data = tb_read_glb(self->std_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  // Parse expected mesh from glb
  self->ocean_patch_mesh =
      tb_mesh_system_load_mesh(mesh_system, asset_path, &data->meshes[0]);

  // Create ocean pipeline layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkResult err = tb_rnd_create_pipeline_layout(render_system, &create_info,
                                                 "Ocean Pipeline Layout",
                                                 &self->pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create ocean pipeline layout", err);
  }

  return true;
}

void destroy_ocean_system(OceanSystem *self) {
  tb_mesh_system_release_mesh_ref(self->mesh_system, self->ocean_patch_mesh);

  tb_rnd_destroy_pipe_layout(self->render_system, self->pipe_layout);

  *self = (OceanSystem){0};
}

void tick_ocean_system(OceanSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(ocean, OceanSystem, OceanSystemDescriptor)

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc) {
  *desc = (SystemDescriptor){
      .name = "Ocean",
      .size = sizeof(OceanSystem),
      .id = OceanSystemId,
      .desc = (InternalDescriptor)ocean_desc,
      .dep_count = 1,
      .deps[0] = (SystemComponentDependencies){1, {OceanComponentId}},
      .system_dep_count = 2,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = MeshSystemId,
      .create = tb_create_ocean_system,
      .destroy = tb_destroy_ocean_system,
      .tick = tb_tick_ocean_system,
  };
}
