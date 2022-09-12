#include "meshsystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
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

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_mesh *mesh) {
  // Hash the mesh's path and gltf name to get the id
  TbMeshId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)mesh->name, SDL_strlen(mesh->name));

  uint32_t index = find_mesh_by_id(self, id);

  // Mesh was not found, load it now
  if (index == SDL_MAX_UINT32) {
    const uint32_t new_count = self->mesh_count + 1;
    if (new_count > self->mesh_max) {
      // Re-allocate space for meshes
      const uint32_t new_max = new_count * 2;

      Allocator alloc = self->std_alloc;

      self->mesh_ids =
          tb_realloc_nm_tp(alloc, self->mesh_ids, new_max, TbMeshId);
      self->mesh_host_buffers = tb_realloc_nm_tp(alloc, self->mesh_host_buffers,
                                                 new_max, TbHostBuffer);
      self->mesh_gpu_buffers =
          tb_realloc_nm_tp(alloc, self->mesh_gpu_buffers, new_max, TbBuffer);
      self->mesh_ref_counts =
          tb_realloc_nm_tp(alloc, self->mesh_ref_counts, new_max, uint32_t);

      self->mesh_max = new_max;
    }

    index = self->mesh_count;

    // Determine how big this mesh is
    uint64_t geom_size = 0;
    uint64_t vertex_offset = 0;
    uint32_t attrib_count = 0;
    uint64_t input_perm = 0;
    {
      uint64_t index_size = 0;
      uint64_t vertex_size = 0;
      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];
        cgltf_accessor *indices = prim->indices;

        index_size += indices->buffer_view->size;

        for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
             ++attr_idx) {
          // Only care about certain attributes at the moment
          cgltf_attribute_type type = prim->attributes[attr_idx].type;
          int32_t index = prim->attributes[attr_idx].index;
          if ((type == cgltf_attribute_type_position ||
               type == cgltf_attribute_type_normal ||
               type == cgltf_attribute_type_tangent ||
               type == cgltf_attribute_type_texcoord) &&
              index == 0) {
            cgltf_accessor *attr = prim->attributes[attr_idx].data;
            vertex_size += attr->count * attr->stride;

            if (type == cgltf_attribute_type_position) {
              input_perm |= VA_INPUT_PERM_POSITION;
            } else if (type == cgltf_attribute_type_normal) {
              input_perm |= VA_INPUT_PERM_NORMAL;
            } else if (type == cgltf_attribute_type_tangent) {
              input_perm |= VA_INPUT_PERM_TANGENT;
            } else if (type == cgltf_attribute_type_texcoord) {
              input_perm |= VA_INPUT_PERM_TEXCOORD0;
            }

            attrib_count++;
          }
        }

        // Calculate the necessary padding between the index and vertex contents
        // of the buffer.
        // Otherwise we'll get a validation error.
        // The vertex content needs to start that the correct attribAddress
        // which must be a multiple of the size of the first attribute
        uint64_t idx_padding = index_size % (sizeof(float) * 3);
        vertex_offset = index_size + idx_padding;
        geom_size = vertex_offset + vertex_size;
      }
    }

    VkResult err = VK_SUCCESS;

    // Allocate space on the host that we can read the mesh into
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = geom_size,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      };

      const uint32_t name_max = 100;
      char name[name_max] = {0};
      SDL_snprintf(name, name_max, "%s Host Geom Buffer", mesh->name);

      err = tb_rnd_sys_alloc_host_buffer(self->render_system, &create_info,
                                         name, &self->mesh_host_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to create host mesh buffer", false);
    }

    // Create space on the gpu for the mesh
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = geom_size,
          .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      };

      const uint32_t name_max = 100;
      char name[name_max] = {0};
      SDL_snprintf(name, name_max, "%s GPU Geom Buffer", mesh->name);

      err = tb_rnd_sys_alloc_gpu_buffer(self->render_system, &create_info, name,
                                        &self->mesh_gpu_buffers[index]);
      TB_VK_CHECK_RET(err, "Failed to create gpu mesh buffer", false);
    }

    // Read the cgltf mesh into the driver owned memory
    {
      TbHostBuffer *host_buf = &self->mesh_host_buffers[index];
      uint64_t idx_offset = 0;
      uint64_t vtx_offset = vertex_offset;
      for (cgltf_size prim_idx = 0; prim_idx < mesh->primitives_count;
           ++prim_idx) {
        cgltf_primitive *prim = &mesh->primitives[prim_idx];

        {
          cgltf_accessor *indices = prim->indices;
          cgltf_buffer_view *view = indices->buffer_view;
          cgltf_size src_offset = indices->offset + view->offset;
          cgltf_size index_size = view->size;

          void *src = ((uint8_t *)view->buffer->data) + src_offset;
          void *dst = ((uint8_t *)(host_buf->ptr)) + idx_offset;
          SDL_memcpy(dst, src, index_size);
          idx_offset += index_size;
        }

        // Reorder attributes
        uint32_t *attr_order =
            tb_alloc(self->tmp_alloc, sizeof(uint32_t) * attrib_count);
        for (uint32_t i = 0; i < (uint32_t)prim->attributes_count; ++i) {
          cgltf_attribute_type attr_type = prim->attributes[i].type;
          int32_t attr_idx = prim->attributes[i].index;
          if (attr_type == cgltf_attribute_type_position) {
            attr_order[0] = i;
          } else if (attr_type == cgltf_attribute_type_normal) {
            attr_order[1] = i;
          } else if (attr_type == cgltf_attribute_type_tangent) {
            attr_order[2] = i;
          } else if (attr_type == cgltf_attribute_type_texcoord &&
                     attr_idx == 0) {
            if (input_perm & VA_INPUT_PERM_TANGENT) {
              attr_order[3] = i;
            } else {
              attr_order[2] = i;
            }
          }
        }

        for (cgltf_size attr_idx = 0; attr_idx < attrib_count; ++attr_idx) {
          cgltf_attribute *attr = &prim->attributes[attr_order[attr_idx]];
          cgltf_accessor *accessor = attr->data;
          cgltf_buffer_view *view = accessor->buffer_view;

          size_t attr_offset = view->offset + accessor->offset;
          size_t attr_size = accessor->stride * accessor->count;

          // TODO: Figure out how to handle when an object can't use the
          // expected pipeline
          if (SDL_strcmp(attr->name, "NORMAL") == 0) {
            if (attr_idx + 1 < prim->attributes_count) {
              cgltf_attribute *next =
                  &prim->attributes[attr_order[attr_idx + 1]];
              if (input_perm & VA_INPUT_PERM_TANGENT) {
                if (SDL_strcmp(next->name, "TANGENT") != 0) {
                  SDL_TriggerBreakpoint();
                }
              } else {
                if (SDL_strcmp(next->name, "TEXCOORD_0") != 0) {
                  SDL_TriggerBreakpoint();
                }
              }
            }
          }

          void *src = ((uint8_t *)view->buffer->data) + attr_offset;
          void *dst = ((uint8_t *)(host_buf->ptr)) + vtx_offset;
          SDL_memcpy(dst, src, attr_size);
          vtx_offset += attr_size;
        }
      }
    }

    // Instruct the render system to upload this
    {
      BufferCopy copy = {
          .src = self->mesh_host_buffers[index].buffer,
          .dst = self->mesh_gpu_buffers[index].buffer,
          .region = {.size = geom_size},
      };
      tb_rnd_upload_buffers(self->render_system, &copy, 1);
    }

    self->mesh_ids[index] = id;
    self->mesh_ref_counts[index] = 1;

    self->mesh_count++;
  }

  return id;
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
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->render_system->vma_alloc;

    TbHostBuffer *host_buf = &self->mesh_host_buffers[index];
    TbBuffer *gpu_buf = &self->mesh_gpu_buffers[index];

    vmaUnmapMemory(vma_alloc, host_buf->alloc);

    vmaDestroyBuffer(vma_alloc, host_buf->buffer, host_buf->alloc);
    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);

    *host_buf = (TbHostBuffer){0};
    *gpu_buf = (TbBuffer){0};
  }
}
