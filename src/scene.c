#include "scene.h"
#include "cpuresources.h"
#include "gpuresources.h"
#include "tbsdl.h"

#include <assert.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <stdbool.h>
#include <stdlib.h>

#include "tbgltf.h"

static cgltf_result
sdl_read_gltf(const struct cgltf_memory_options *memory_options,
              const struct cgltf_file_options *file_options, const char *path,
              cgltf_size *size, void **data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;
  cgltf_size file_size = (cgltf_size)SDL_RWsize(file);
  (void)path;

  void *mem = memory_options->alloc_func(memory_options->user_data, file_size);
  if (mem == NULL) {
    assert(0);
    return cgltf_result_out_of_memory;
  }

  if (SDL_RWread(file, mem, file_size, 1) == 0) {
    return cgltf_result_io_error;
  }

  *size = file_size;
  *data = mem;

  return cgltf_result_success;
}

static void sdl_release_gltf(const struct cgltf_memory_options *memory_options,
                             const struct cgltf_file_options *file_options,
                             void *data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;

  memory_options->free_func(memory_options->user_data, data);

  if (SDL_RWclose(file) != 0) {
    assert(0);
  }
}

int32_t create_scene(DemoAllocContext alloc_ctx, Scene *out_scene) {
  (*out_scene) = (Scene){.alloc_ctx = alloc_ctx};
  return 0;
}

int32_t scene_append_gltf(Scene *s, const char *filename) {
  const DemoAllocContext *alloc_ctx = &s->alloc_ctx;
  VkDevice device = alloc_ctx->device;
  Allocator std_alloc = alloc_ctx->std_alloc;
  Allocator tmp_alloc = alloc_ctx->tmp_alloc;
  VmaAllocator vma_alloc = alloc_ctx->vma_alloc;
  const VkAllocationCallbacks *vk_alloc = alloc_ctx->vk_alloc;
  VmaPool up_pool = alloc_ctx->up_pool;
  VmaPool tex_pool = alloc_ctx->tex_pool;

  // Load a GLTF/GLB file off disk
  cgltf_data *data = NULL;
  {
    // We really only want to handle glbs; gltfs should be pre-packed
    SDL_RWops *gltf_file = SDL_RWFromFile(filename, "rb");

    if (gltf_file == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", SDL_GetError());
      assert(0);
      return -1;
    }

    cgltf_options options = {.type = cgltf_file_type_glb,
                             .memory =
                                 {
                                     .user_data = std_alloc.user_data,
                                     .alloc_func = std_alloc.alloc,
                                     .free_func = std_alloc.free,
                                 },
                             .file = {
                                 .read = sdl_read_gltf,
                                 .release = sdl_release_gltf,
                                 .user_data = gltf_file,
                             }};

    // Parse file loaded via SDL
    cgltf_result res = cgltf_parse_file(&options, filename, &data);
    if (res != cgltf_result_success) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", "Failed to parse gltf");
      SDL_TriggerBreakpoint();
      return -1;
    }

    res = cgltf_load_buffers(&options, data, filename);
    if (res != cgltf_result_success) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", "Failed to load gltf buffers");
      SDL_TriggerBreakpoint();
      return -2;
    }

    // TODO: Only do this on non-final builds
    res = cgltf_validate(data);
    if (res != cgltf_result_success) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to load validate gltf");
      SDL_TriggerBreakpoint();
      return -3;
    }
  }

  // Collect some pre-append counts so that we can do math later
  cgltf_size old_tex_count = s->texture_count;
  cgltf_size old_mat_count = s->material_count;
  cgltf_size old_mesh_count = s->mesh_count;
  cgltf_size old_node_count = s->entity_count;

  cgltf_size new_mat_count = old_mat_count + data->materials_count;

  // Append textures to scene
  {
    cgltf_size new_tex_count = old_tex_count + data->textures_count;

    if (new_tex_count > 0) {
      s->textures =
          tb_realloc_nm_tp(std_alloc, s->textures, new_tex_count, GPUTexture);
      if (s->textures == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to allocate textures for scene");
        SDL_TriggerBreakpoint();
        return -4;
      }
    }

    // TODO: Determine a good way to do texture de-duplication
    for (cgltf_size i = old_tex_count; i < new_tex_count; ++i) {
      cgltf_texture *tex = &data->textures[i - old_tex_count];

      /*
        Awful hack; we don't know what color space these textures want
        Normal maps need UNORM while Color textures need SRGB. So the only
        way for us to get this info is to see if it's used as a color
        texture by any material.

        Textures *could* be created during material discovery instead but that's
        already messy.
      */
      VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
      for (cgltf_size ii = old_mat_count; ii < new_mat_count; ++ii) {
        cgltf_material *mat = &data->materials[ii - old_mat_count];

        if ((mat->has_pbr_metallic_roughness &&
             mat->pbr_metallic_roughness.base_color_texture.texture == tex) ||
            (mat->has_pbr_specular_glossiness &&
             mat->pbr_specular_glossiness.diffuse_texture.texture == tex)) {
          format = VK_FORMAT_R8G8B8A8_SRGB;
          break;
        }
      }

      if (create_gputexture_cgltf(device, vma_alloc, vk_alloc, tex, data->bin,
                                  up_pool, tex_pool, format,
                                  &s->textures[i]) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to to create gputexture");
        SDL_TriggerBreakpoint();
        return -5;
      }
    }

    s->texture_count = (uint32_t)new_tex_count;
  }

  // Append materials to scene
  {
    if (new_mat_count > 0) {
      s->materials =
          tb_realloc_nm_tp(std_alloc, s->materials, new_mat_count, GPUMaterial);
      if (s->materials == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to allocate textures for scene");
        SDL_TriggerBreakpoint();
        return -4;
      }
    }

    for (cgltf_size i = old_mat_count; i < new_mat_count; ++i) {
      cgltf_material *mat = &data->materials[i - old_mat_count];

      // Need to determine which textures the material uses
      // Worst-case the material uses 8 textures
      uint32_t mat_tex_refs[MAX_MATERIAL_TEXTURES] = {0};
      memset(mat_tex_refs, (int32_t)0xFFFFFFFF,
             sizeof(uint32_t) * MAX_MATERIAL_TEXTURES);
      uint32_t mat_tex_count = collect_material_textures(
          (uint32_t)data->textures_count, data->textures, mat,
          (uint32_t)old_tex_count, mat_tex_refs);

      if (create_gpumaterial_cgltf(device, vma_alloc, vk_alloc, mat,
                                   mat_tex_count, mat_tex_refs,
                                   &s->materials[i]) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to to create gpumaterial");
        SDL_TriggerBreakpoint();
        return -5;
      }
    }

    s->material_count = (uint32_t)new_mat_count;
  }

  // Append meshes to scene
  {
    cgltf_size new_mesh_count = old_mesh_count + data->meshes_count;

    s->meshes = tb_realloc_nm_tp(std_alloc, s->meshes, new_mesh_count, GPUMesh);
    if (s->meshes == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to allocate meshes for scene");
      SDL_TriggerBreakpoint();
      return -4;
    }

    // TODO: Determine a good way to do mesh de-duplication
    for (cgltf_size i = old_mesh_count; i < new_mesh_count; ++i) {
      cgltf_mesh *mesh = &data->meshes[i - old_mesh_count];
      if (create_gpumesh_cgltf(device, vma_alloc, tmp_alloc, mesh,
                               &s->meshes[i]) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to to create gpumesh");
        SDL_TriggerBreakpoint();
        return -5;
      }
    }

    s->mesh_count = (uint32_t)new_mesh_count;
  }

  // Append nodes to scene
  {
    cgltf_size new_node_count = old_node_count + data->nodes_count;

    // Alloc entity component rows
    s->components =
        tb_realloc_nm_tp(std_alloc, s->components, new_node_count, uint64_t);
    s->static_mesh_refs = tb_realloc_nm_tp(std_alloc, s->static_mesh_refs,
                                           new_node_count, uint32_t);
    s->material_refs =
        tb_realloc_nm_tp(std_alloc, s->material_refs, new_node_count, uint32_t);
    s->transforms = tb_realloc_nm_tp(std_alloc, s->transforms, new_node_count,
                                     SceneTransform);

    for (cgltf_size i = old_node_count; i < new_node_count; ++i) {
      cgltf_node *node = &data->nodes[i - old_node_count];

      // For now, all nodes have transforms
      {
        // Assign a transform component
        s->components[i] |= COMPONENT_TYPE_TRANSFORM;

        {
          s->transforms[i].t.rotation[0] = node->rotation[0];
          s->transforms[i].t.rotation[1] = node->rotation[1];
          s->transforms[i].t.rotation[2] = node->rotation[2];
        }
        {
          s->transforms[i].t.scale[0] = node->scale[0];
          s->transforms[i].t.scale[1] = node->scale[1];
          s->transforms[i].t.scale[2] = node->scale[2];
        }
        {
          s->transforms[i].t.position[0] = node->translation[0];
          s->transforms[i].t.position[1] = node->translation[1];
          s->transforms[i].t.position[2] = node->translation[2];
        }

        // Appending a gltf to an existing scene means there should be no
        // references between scenes. We should be safe to a just create
        // children.
        s->transforms[i].child_count = (uint32_t)node->children_count;

        if (node->children_count >= MAX_CHILD_COUNT) {
          SDL_LogError(
              SDL_LOG_CATEGORY_ERROR,
              "Node has number of children that exceeds max child count of: %d",
              MAX_CHILD_COUNT);
          SDL_TriggerBreakpoint();
          return -6;
        }

        // Go through all of the gltf node's children
        // We want to find the index of the child, relative to the gltf node
        // From there we can determine the index of the child relative to the
        // scene node
        for (cgltf_size ii = 0; ii < node->children_count; ++ii) {
          for (cgltf_size iii = 0; iii < data->nodes_count; ++iii) {
            if (&data->nodes[iii] == node->children[ii]) {
              s->transforms[i].children[ii] = (uint32_t)(old_node_count + iii);
              break;
            }
          }
        }
      }

      // Does this node have an associated mesh?
      if (node->mesh) {
        // Assign a static mesh component
        s->components[i] |= COMPONENT_TYPE_STATIC_MESH;

        // Find the index of the mesh that matches what this node wants
        for (cgltf_size ii = 0; ii < data->meshes_count; ++ii) {
          if (node->mesh == &data->meshes[ii]) {
            s->static_mesh_refs[i] = (uint32_t)(old_mesh_count + ii);
            break;
          }
        }
        // Find the index of the material that matches what this node wants
        for (cgltf_size ii = 0; ii < data->materials_count; ++ii) {
          // TODO: Iterate over primitives
          if (node->mesh->primitives[0].material == &data->materials[ii]) {
            s->material_refs[i] = (uint32_t)(old_mat_count + ii);
          }
        }
      }

      // TODO: Lights, cameras, (action!)
    }

    s->entity_count = (uint32_t)new_node_count;
  }

  cgltf_free(data);

  return 0;
}

void destroy_scene(Scene *s) {
  const DemoAllocContext *alloc_ctx = &s->alloc_ctx;
  VkDevice device = alloc_ctx->device;
  Allocator std_alloc = alloc_ctx->std_alloc;
  VmaAllocator vma_alloc = alloc_ctx->vma_alloc;
  const VkAllocationCallbacks *vk_alloc = alloc_ctx->vk_alloc;

  // Clean up GPU memory
  for (uint32_t i = 0; i < s->mesh_count; ++i) {
    destroy_gpumesh(vma_alloc, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->material_count; ++i) {
    destroy_material(device, vma_alloc, vk_alloc, &s->materials[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; ++i) {
    destroy_texture(device, vma_alloc, vk_alloc, &s->textures[i]);
  }

  // Clean up CPU-side arrays
  tb_free(std_alloc, s->materials);
  s->materials = NULL;
  s->material_count = 0;
  tb_free(std_alloc, s->meshes);
  s->meshes = NULL;
  s->mesh_count = 0;
  tb_free(std_alloc, s->textures);
  s->textures = NULL;
  s->texture_count = 0;
  tb_free(std_alloc, s->components);
  s->components = NULL;
  s->entity_count = 0;
  tb_free(std_alloc, s->static_mesh_refs);
  s->static_mesh_refs = NULL;
  tb_free(std_alloc, s->material_refs);
  s->material_refs = NULL;
  tb_free(std_alloc, s->transforms);
  s->transforms = NULL;
}
