#include "meshsystem.h"

#include "meshcomponent.h"
#include "rendersystem.h"
#include "world.h"

bool create_mesh_system(MeshSystem *self, const MeshSystemDescriptor *desc,
                        uint32_t system_dep_count, System *const *system_deps) {
  // Find the render system
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which meshes depends on",
                  VK_ERROR_UNKNOWN);

  *self = (MeshSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  return true;
}

void destroy_mesh_system(MeshSystem *self) {
  RenderSystem *render_system = self->render_system;
  (void)render_system;

  /* TODO:
for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
  vkDestroyFramebuffer(render_system->render_thread->device,
                       self->framebuffers[i],
                       &render_system->vk_host_alloc_cb);
}

tb_rnd_destroy_render_pass(render_system, self->pass);

tb_rnd_destroy_sampler(render_system, self->sampler);
tb_rnd_destroy_set_layout(render_system, self->set_layout);
tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
tb_rnd_destroy_pipeline(render_system, self->pipeline);
*/

  *self = (MeshSystem){0};
}

void tick_mesh_system(MeshSystem *self, const SystemInput *input,
                      SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(mesh, MeshSystem, MeshSystemDescriptor)

void tb_mesh_system_descriptor(SystemDescriptor *desc,
                               const MeshSystemDescriptor *mesh_desc) {
  desc->name = "Mesh";
  desc->size = sizeof(MeshSystem);
  desc->id = MeshSystemId;
  desc->desc = (InternalDescriptor)mesh_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {MeshComponentId},
  };
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_mesh_system;
  desc->destroy = tb_destroy_mesh_system;
  desc->tick = tb_tick_mesh_system;
}
