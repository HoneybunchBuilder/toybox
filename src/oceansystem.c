#include "oceansystem.h"

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
                  "Failed to find render system which ocean depend on", false);

  *self = (OceanSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Create a square patch of triangles to use as an ocean plane mesh
  uint64_t patch_size = 100; // TODO
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = patch_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    err = tb_rnd_sys_alloc_host_buffer(render_system, &create_info,
                                       "Ocean Patch Host",
                                       &self->host_patch_geom);
    TB_VK_CHECK_RET(err, "Failed to create host patch buffer", false);
  }
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = patch_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    };
    err = tb_rnd_sys_alloc_gpu_buffer(render_system, &create_info,
                                      "Ocean Patch GPU", &self->gpu_patch_geom);
    TB_VK_CHECK_RET(err, "Failed to create gpu patch buffer", false);
  }
  // Copy the patch geometry to the gpu
  {
    BufferCopy copy = {
        .src = self->host_patch_geom.buffer,
        .dst = self->gpu_patch_geom.buffer,
        .region = {.size = patch_size},
    };
    tb_rnd_upload_buffers(render_system, &copy, 1);
  }

  return true;
}

void destroy_ocean_system(OceanSystem *self) {
  VmaAllocator vma_alloc = self->render_system->vma_alloc;

  vmaUnmapMemory(vma_alloc, self->host_patch_geom.alloc);

  vmaDestroyBuffer(vma_alloc, self->host_patch_geom.buffer,
                   self->host_patch_geom.alloc);
  vmaDestroyBuffer(vma_alloc, self->gpu_patch_geom.buffer,
                   self->gpu_patch_geom.alloc);
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
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_ocean_system,
      .destroy = tb_destroy_ocean_system,
      .tick = tb_tick_ocean_system,
  };
}
