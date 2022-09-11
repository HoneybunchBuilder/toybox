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

uint32_t find_mesh_by_id(MeshSystem *self, TbMeshId id) {
  for (uint32_t i = 0; i < self->mesh_count; ++i) {
    if (self->mesh_ids[i] == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

bool tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                              const cgltf_mesh *mesh, TbMeshId *id) {
  return true;
}

bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh", false);

  self->mesh_ref_counts[index]++;

  return true;
}

VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);
  TB_CHECK_RETURN(index != SDL_MAX_UINT32, "Failed to find mesh",
                  VK_NULL_HANDLE);

  VkBuffer buffer = self->mesh_gpu_buffers[index].buffer;
  TB_CHECK_RETURN(buffer, "Failed to retrieve buffer", VK_NULL_HANDLE);

  return buffer;
}

void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id) {
  uint32_t index = find_mesh_by_id(self, id);

  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find mesh");
    return;
  }

  if (self->mesh_ref_counts[index] == 0) {
    TB_CHECK(false, "Tried to release reference to mesh with 0 ref count");
    return;
  }

  self->mesh_ref_counts[index]--;

  if (self->mesh_ref_counts[index] == 0) {
    // TODO: Actually free mesh
  }

  return;
}
