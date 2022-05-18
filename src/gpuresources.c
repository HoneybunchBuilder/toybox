#include "gpuresources.h"

#include "allocator.h"
#include "cpuresources.h"
#include "profiling.h"
#include "vkdbg.h"
#include "tbgltf.h"
#include "tbvma.h"
#include "tbsdl.h"
#include "tbktx.h"

#include "common.hlsli"
#include "gltf.hlsli"

#include <volk.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int32_t create_gpubuffer(VmaAllocator allocator, uint64_t size,
                         int32_t mem_usage, uint32_t buf_usage, GPUBuffer *out) {
  VkResult err = VK_SUCCESS;
  VkBuffer buffer = {0};
  VmaAllocation alloc = {0};
  VmaAllocationInfo alloc_info = {0};
  {
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = mem_usage;
    VkBufferCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = size;
    create_info.usage = buf_usage;
    err = vmaCreateBuffer(allocator, &create_info, &alloc_create_info, &buffer,
                          &alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }
  *out = (GPUBuffer){buffer, alloc};

  return err;
}

void destroy_gpubuffer(VmaAllocator allocator, const GPUBuffer *buffer) {
  vmaDestroyBuffer(allocator, buffer->buffer, buffer->alloc);
}

static GPUConstBuffer create_gpushaderbuffer(VkDevice device, VmaAllocator allocator,
                                      const VkAllocationCallbacks *vk_alloc,
                                      uint64_t size, VkBufferUsageFlags usage) {
  GPUBuffer host_buffer = {0};
  VkResult err =
      create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
  assert(err == VK_SUCCESS);
  (void)err;

  GPUBuffer device_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_GPU_ONLY,
                         usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &device_buffer);
  assert(err == VK_SUCCESS);

  VkSemaphore sem = VK_NULL_HANDLE;
  {
    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    err = vkCreateSemaphore(device, &create_info, vk_alloc, &sem);
    assert(err == VK_SUCCESS);
  }

  GPUConstBuffer cb = {
      .size = size,
      .host = host_buffer,
      .gpu = device_buffer,
      .updated = sem,
  };
  return cb;
}

GPUConstBuffer create_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                                     const VkAllocationCallbacks *vk_alloc,
                                     uint64_t size) {
  return create_gpushaderbuffer(device, allocator, vk_alloc, size,
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

GPUConstBuffer create_gpustoragebuffer(VkDevice device, VmaAllocator allocator,
                                       const VkAllocationCallbacks *vk_alloc,
                                       uint64_t size) {
  return create_gpushaderbuffer(device, allocator, vk_alloc, size,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void destroy_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                            const VkAllocationCallbacks *vk_alloc,
                            GPUConstBuffer cb) {
  destroy_gpubuffer(allocator, &cb.host);
  destroy_gpubuffer(allocator, &cb.gpu);
  vkDestroySemaphore(device, cb.updated, vk_alloc);
}

int32_t create_gpumesh(VmaAllocator vma_alloc, uint64_t input_perm,
                       const CPUMesh *src_mesh, GPUMesh *dst_mesh) {
  TracyCZoneN(prof_e, "create_gpumesh", true)
  VkResult err = VK_SUCCESS;

  size_t index_size = src_mesh->index_size;
  size_t geom_size = src_mesh->geom_size;

  size_t size = index_size + geom_size;

  GPUBuffer host_buffer = {0};
  err = create_gpubuffer(vma_alloc, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
  assert(err == VK_SUCCESS);

  GPUBuffer device_buffer = {0};
  err = create_gpubuffer(vma_alloc, size, VMA_MEMORY_USAGE_GPU_ONLY,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &device_buffer);
  assert(err == VK_SUCCESS);

  // Actually copy cube data to cpu local buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);

    // Copy Data
    memcpy(data, src_mesh->indices, size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }

  dst_mesh->surface_count = 1;
  dst_mesh->surfaces[0] = (GPUSurface){input_perm,
                                       src_mesh->index_count,
                                       src_mesh->vertex_count,
                                       VK_INDEX_TYPE_UINT16,
                                       size,
                                       src_mesh->index_size,
                                       src_mesh->geom_size,
                                       host_buffer,
                                       device_buffer};

  TracyCZoneEnd(prof_e)
  return err;
}

int32_t create_gpumesh_cgltf(VkDevice device, VmaAllocator vma_alloc,
                             Allocator tmp_alloc, const cgltf_mesh *src_mesh,
                             GPUMesh *dst_mesh) {
  TracyCZoneN(prof_e, "create_gpumesh_cgltf", true)
  assert(src_mesh->primitives_count < MAX_SURFACE_COUNT);
  cgltf_size surface_count = src_mesh->primitives_count;
  if (surface_count > MAX_SURFACE_COUNT) {
    surface_count = MAX_SURFACE_COUNT;
  }

  VkResult err = VK_SUCCESS;

  for (cgltf_size i = 0; i < surface_count; ++i) {
    cgltf_primitive *prim = &src_mesh->primitives[i];
    cgltf_accessor *indices = prim->indices;

    cgltf_size index_count = indices->count;
    cgltf_size vertex_count = prim->attributes[0].data->count;

    int32_t index_type = VK_INDEX_TYPE_UINT16;
    if (indices->stride > 2) {
      index_type = VK_INDEX_TYPE_UINT32;
    }

    cgltf_size index_size = indices->buffer_view->size;
    cgltf_size geom_size = 0;
    uint64_t input_perm = 0;
    // Only allow certain attributes for now
    uint32_t attrib_count = 0;
    for (cgltf_size ii = 0; ii < prim->attributes_count; ++ii) {
      cgltf_attribute_type type = prim->attributes[ii].type;
      int32_t index = prim->attributes[ii].index;
      if ((type == cgltf_attribute_type_position ||
           type == cgltf_attribute_type_normal ||
           type == cgltf_attribute_type_tangent ||
           type == cgltf_attribute_type_texcoord) &&
          index == 0) {
        cgltf_accessor *attr = prim->attributes[ii].data;
        geom_size += attr->count * attr->stride;

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
    size_t idx_padding = index_size % (sizeof(float) * 3);

    size_t size = index_size + idx_padding + geom_size;

    GPUBuffer host_buffer = {0};
    err = create_gpubuffer(vma_alloc, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
    assert(err == VK_SUCCESS);

    GPUBuffer device_buffer = {0};
    err = create_gpubuffer(vma_alloc, size, VMA_MEMORY_USAGE_GPU_ONLY,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           &device_buffer);
    assert(err == VK_SUCCESS);

    // Actually copy cube data to cpu local buffer
    {
      uint8_t *data = NULL;
      vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);

      size_t offset = 0;
      // Copy Index Data
      {
        cgltf_buffer_view *view = indices->buffer_view;
        size_t index_offset = indices->offset + view->offset;

        void *index_data = ((uint8_t *)view->buffer->data) + index_offset;
        memcpy(data, index_data, index_size);
        offset += index_size;
        offset += idx_padding;
      }

      // Reorder attributes
      uint32_t *attr_order =
          hb_alloc(tmp_alloc, sizeof(uint32_t) * attrib_count);
      for (uint32_t ii = 0; ii < (uint32_t)prim->attributes_count; ++ii) {
        cgltf_attribute_type attr_type = prim->attributes[ii].type;
        int32_t attr_idx = prim->attributes[ii].index;
        if (attr_type == cgltf_attribute_type_position) {
          attr_order[0] = ii;
        } else if (attr_type == cgltf_attribute_type_normal) {
          attr_order[1] = ii;
        } else if (attr_type == cgltf_attribute_type_tangent) {
          attr_order[2] = ii;
        } else if (attr_type == cgltf_attribute_type_texcoord &&
                   attr_idx == 0) {
          if (input_perm & VA_INPUT_PERM_TANGENT) {
            attr_order[3] = ii;
          } else {
            attr_order[2] = ii;
          }
        }
      }

      for (cgltf_size ii = 0; ii < attrib_count; ++ii) {
        uint32_t attr_idx = attr_order[ii];
        cgltf_attribute *attr = &prim->attributes[attr_idx];
        cgltf_accessor *accessor = attr->data;
        cgltf_buffer_view *view = accessor->buffer_view;

        size_t attr_offset = view->offset + accessor->offset;
        size_t attr_size = accessor->stride * accessor->count;

        // TODO: Figure out how to handle when an object can't use the expected
        // pipeline
        if (SDL_strcmp(attr->name, "NORMAL") == 0) {
          if (ii + 1 < prim->attributes_count) {
            cgltf_attribute *next = &prim->attributes[attr_order[ii + 1]];
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

        void *attr_data = ((uint8_t *)view->buffer->data) + attr_offset;
        memcpy(data + offset, attr_data, attr_size);
        offset += attr_size;
      }

      SDL_assert(offset == size);
    }

    dst_mesh->surfaces[i] =
        (GPUSurface){input_perm, index_count, vertex_count, index_type,   size,
                     index_size, geom_size,   host_buffer,  device_buffer};

    // Set some debug names on the vulkan primitives
    {
      static const uint32_t max_name_size = 128;
      char *host_name = hb_alloc_nm_tp(tmp_alloc, max_name_size, char);
      SDL_snprintf(host_name, max_name_size, "%s surface %d @host",
                   src_mesh->name, i);
      SET_VK_NAME(device, host_buffer.buffer, VK_OBJECT_TYPE_BUFFER, host_name);

      char *device_name = hb_alloc_nm_tp(tmp_alloc, max_name_size, char);
      SDL_snprintf(device_name, max_name_size, "%s surface %d @device",
                   src_mesh->name, i);
      SET_VK_NAME(device, device_buffer.buffer, VK_OBJECT_TYPE_BUFFER,
                  device_name);
    }

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }
  dst_mesh->surface_count = (uint32_t)surface_count;

  TracyCZoneEnd(prof_e)
  return err;
}

void destroy_gpumesh(VmaAllocator vma_alloc, const GPUMesh *mesh) {
  for (uint32_t i = 0; i < mesh->surface_count; ++i) {
    destroy_gpubuffer(vma_alloc, &mesh->surfaces[i].host);
    destroy_gpubuffer(vma_alloc, &mesh->surfaces[i].gpu);
  }
}

int32_t create_gpuimage(VmaAllocator vma_alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,
                        GPUImage *i) {
  TracyCZoneN(prof_e, "create_gpuimage", true)
  VkResult err = VK_SUCCESS;
  GPUImage img = {0};

  VmaAllocationInfo alloc_info = {0};
  err = vmaCreateImage(vma_alloc, img_create_info, alloc_create_info,
                       &img.image, &img.alloc, &alloc_info);
  assert(err == VK_SUCCESS);

  if (err == VK_SUCCESS) {
    *i = img;
  }
  TracyCZoneEnd(prof_e)
  return err;
}

void destroy_gpuimage(VmaAllocator allocator, const GPUImage *image) {
  vmaDestroyImage(allocator, image->image, image->alloc);
}

static SDL_Surface *load_and_transform_image(const char *filename) {
  TracyCZoneN(prof_e, "load_and_transform_image", true)
  SDL_Surface *img = IMG_Load(filename);
  assert(img);

  SDL_PixelFormat *opt_fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

  SDL_Surface *opt_img = SDL_ConvertSurface(img, opt_fmt, 0);
  SDL_FreeSurface(img);

  TracyCZoneEnd(prof_e)
  return opt_img;
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
static SDL_Surface *parse_and_transform_image(const uint8_t *data, size_t size) {
  SDL_RWops *ops = SDL_RWFromMem((void *)data, (int32_t)size);
  SDL_Surface *img = IMG_Load_RW(ops, 0);
  if (!img) {
    const char *err = IMG_GetError();
    (void)err;
    assert(false);
    return NULL;
  }

  SDL_PixelFormat *opt_fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

  SDL_Surface *opt_img = SDL_ConvertSurface(img, opt_fmt, 0);
  SDL_FreeSurface(img);

  return opt_img;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

static VkImageType get_ktx2_image_type(const ktxTexture2 *t) {
  return (VkImageType)(t->numDimensions - 1);
}

static VkImageViewType get_ktx2_image_view_type(const ktxTexture2 *t) {
  VkImageType img_type = get_ktx2_image_type(t);

  bool cube = t->isCubemap;
  bool array = t->isArray;

  if (img_type == VK_IMAGE_TYPE_1D) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    } else {
      return VK_IMAGE_VIEW_TYPE_1D;
    }
  } else if (img_type == VK_IMAGE_TYPE_2D) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
      return VK_IMAGE_VIEW_TYPE_2D;
    }

  } else if (img_type == VK_IMAGE_TYPE_3D) {
    // No such thing as a 3D array
    return VK_IMAGE_VIEW_TYPE_3D;
  } else if (cube) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_CUBE;
  }

  assert(0);
  return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

typedef struct KTX2CbData {
  VkBufferImageCopy *region; // Specify destination region in final image.
  VkDeviceSize offset;       // Offset of current level in staging buffer
  uint32_t num_faces;
  uint32_t num_layers;
} KTX2CbData;

static ktx_error_code_e ktx2_optimal_tiling_callback(
    int32_t mip_level, int32_t face, int32_t width, int32_t height,
    int32_t depth, uint64_t face_lod_size, void *pixels, void *userdata) {
  KTX2CbData *ud = (KTX2CbData *)userdata;
  (void)pixels;

  ud->region->bufferOffset = ud->offset;
  ud->offset += face_lod_size;
  // These 2 are expressed in texels.
  ud->region->bufferRowLength = 0;
  ud->region->bufferImageHeight = 0;
  ud->region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ud->region->imageSubresource.mipLevel = (uint32_t)mip_level;
  ud->region->imageSubresource.baseArrayLayer = (uint32_t)face;
  ud->region->imageSubresource.layerCount = ud->num_layers * ud->num_faces;
  ud->region->imageOffset.x = 0;
  ud->region->imageOffset.y = 0;
  ud->region->imageOffset.z = 0;
  ud->region->imageExtent.width = (uint32_t)width;
  ud->region->imageExtent.height = (uint32_t)height;
  ud->region->imageExtent.depth = (uint32_t)depth;

  ud->region += 1;

  return KTX_SUCCESS;
}

GPUTexture load_ktx2_texture(VkDevice device, VmaAllocator vma_alloc,
                             Allocator *tmp_alloc,
                             const VkAllocationCallbacks *vk_alloc,
                             const char *file_path, VmaPool up_pool,
                             VmaPool tex_pool) {
  TracyCZoneN(prof_e, "load_ktx2_texture", true)
  GPUTexture t = {0};

  uint8_t *mem = NULL;
  size_t size = 0;
  {
    SDL_RWops *file = SDL_RWFromFile(file_path, "rb");
    if (file == NULL) {
      assert(0);
      TracyCZoneEnd(prof_e)
      return t;
    }

    size = (size_t)file->size(file);
    mem = hb_alloc(*tmp_alloc, size);
    assert(mem);

    // Read file into memory
    if (file->read(file, mem, size, 1) == 0) {
      file->close(file);
      assert(0);
      TracyCZoneEnd(prof_e)
      return t;
    }
    file->close(file);
  }

  ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;
  ktxTexture2 *ktx = NULL;
  {
    TracyCZoneN(ktx_transcode_e, "load_ktx2_texture transcode", true)
    ktx_error_code_e err = ktxTexture2_CreateFromMemory(mem, size, flags, &ktx);
    if (err != KTX_SUCCESS) {
      assert(0);
      TracyCZoneEnd(ktx_transcode_e)
      TracyCZoneEnd(prof_e)
      return t;
    }

    bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
    if (needs_transcoding) {
      // TODO: pre-calculate the best format for the platform
      err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
      if (err != KTX_SUCCESS) {
        assert(0);
        TracyCZoneEnd(ktx_transcode_e)
        TracyCZoneEnd(prof_e)
        return t;
      }
    }
    TracyCZoneEnd(ktx_transcode_e)
  }

  VkResult err = VK_SUCCESS;

  size_t host_buffer_size = ktx->dataSize;
  uint32_t width = ktx->baseWidth;
  uint32_t height = ktx->baseHeight;
  uint32_t depth = ktx->baseDepth;
  uint32_t layers = ktx->numLayers;
  uint32_t mip_levels = ktx->numLevels;
  VkFormat format = (VkFormat)ktx->vkFormat;
  bool gen_mips = ktx->generateMipmaps;

  GPUBuffer host_buffer = {0};
  {
    VkBufferCreateInfo buffer_create_info = {0};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = host_buffer_size;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_create_info.pool = up_pool;
    VmaAllocationInfo alloc_info = {0};
    err = vmaCreateBuffer(vma_alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    if (err != VK_SUCCESS) {
      assert(0);
      TracyCZoneEnd(prof_e)
      return t;
    }
  }

  GPUImage device_image = {0};
  {
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // If we need to generate mips we'll need to mark the image as being able to
    // be copied from
    if (gen_mips) {
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo img_info = {0};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = get_ktx2_image_type(ktx);
    img_info.format = format;
    img_info.extent = (VkExtent3D){width, height, depth};
    img_info.mipLevels = mip_levels;
    img_info.arrayLayers = layers;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(vma_alloc, &img_info, &alloc_info, &device_image);
    if (err != VK_SUCCESS) {
      assert(0);
      TracyCZoneEnd(prof_e)
      return t;
    }
  }

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    err = vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);
    if (err != VK_SUCCESS) {
      assert(0);
      TracyCZoneEnd(prof_e)
      return t;
    }

    memcpy(data, ktx->pData, host_buffer_size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }

  // Create Image View
  VkImageView view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = device_image.image;
    create_info.viewType = get_ktx2_image_view_type(ktx);
    create_info.format = format;
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layers};
    err = vkCreateImageView(device, &create_info, vk_alloc, &view);
    if (err != VK_SUCCESS) {
      assert(0);
      TracyCZoneEnd(prof_e)
      return t;
    }
  }

  uint32_t region_count = mip_levels;
  if (gen_mips) {
    region_count = 1;
  }
  assert(region_count < MAX_REGION_COUNT);

  t.host = host_buffer;
  t.device = device_image;
  t.format = (uint32_t)format;
  t.width = width;
  t.height = height;
  t.mip_levels = mip_levels;
  t.gen_mips = gen_mips;
  t.layer_count = layers;
  t.view = view;
  t.region_count = region_count;

  // Gather Copy Regions
  {
    KTX2CbData cb_data = {
        .num_faces = ktx->numFaces,
        .num_layers = ktx->numLayers,
        .region = t.regions,
    };

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#endif
    ktxTexture_IterateLevels(ktx, ktx2_optimal_tiling_callback, &cb_data);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  }

  TracyCZoneEnd(prof_e)
  return t;
}

int32_t load_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const char *filename, VmaPool up_pool, VmaPool tex_pool,
                     GPUTexture *t) {
  TracyCZoneN(prof_e, "load_texture", true)
  assert(filename);
  assert(t);

  SDL_Surface *img = load_and_transform_image(filename);

  VkResult err = VK_SUCCESS;

  uint32_t img_width = (uint32_t)img->w;
  uint32_t img_height = (uint32_t)img->h;

  size_t host_buffer_size = (uint32_t)img->pitch * img_height;

  GPUBuffer host_buffer = {0};
  {
    VkBufferCreateInfo buffer_create_info = {0};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = host_buffer_size;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_create_info.pool = up_pool;
    VmaAllocationInfo alloc_info = {0};
    err = vmaCreateBuffer(vma_alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }

  uint32_t mip_levels = (uint32_t)(floorf(log2f((float)(SDL_max(img_width, img_height))))) + 1;

  GPUImage device_image = {0};
  {
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // If we need to generate mips we'll need to mark the image as being able to
    // be copied from
    if (mip_levels > 1) {
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo img_info = {0};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D; // Assuming for now
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent = (VkExtent3D){img_width, img_height, 1};
    img_info.mipLevels = mip_levels;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(vma_alloc, &img_info, &alloc_info, &device_image);
    assert(err == VK_SUCCESS);
  }

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);

    memcpy(data, img->pixels, host_buffer_size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }

  // Create Image View
  VkImageView view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = device_image.image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};
    err = vkCreateImageView(device, &create_info, vk_alloc, &view);
    assert(err == VK_SUCCESS);
  }

  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_UNORM;
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = mip_levels;
  t->gen_mips = mip_levels > 1;
  t->layer_count = 1;
  t->view = view;
  t->region_count = 1;
  t->regions[0] = (VkBufferImageCopy){
      .imageExtent =
          {
              .width = img_width,
              .height = img_height,
              .depth = 1,
          },
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
          },
  };

  SDL_FreeSurface(img);

  TracyCZoneEnd(prof_e)

  return err;
}

int32_t create_gputexture_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                const VkAllocationCallbacks *vk_alloc,
                                const cgltf_texture *gltf, const uint8_t *bin,
                                VmaPool up_pool, VmaPool tex_pool,
                                VkFormat format, GPUTexture *t) {
  TracyCZoneN(prof_e, "create_gputexture_cgltf", true)
  cgltf_buffer_view *image_view = gltf->image->buffer_view;
  cgltf_buffer *image_data = image_view->buffer;
  const uint8_t *data = (uint8_t *)(image_view->buffer) + image_view->offset;

  if (image_data->uri == NULL) {
    data = bin + image_view->offset;
  }

  size_t size = image_view->size;

  SDL_Surface *image = parse_and_transform_image(data, size);
  uint32_t image_width = (uint32_t)image->w;
  uint32_t image_height = (uint32_t)image->h;
  uint8_t *image_pixels = image->pixels;
  size_t image_size = (uint32_t)image->pitch * image_height;

  TextureMip mip = {
      image_width,
      image_height,
      1,
      image_pixels,
  };

  TextureLayer layer = {
      image_width,
      image_height,
      1,
      &mip,
  };
  CPUTexture cpu_tex = {
      1, 1, &layer, image_size, image_pixels,
  };
  int32_t err = create_texture(device, vma_alloc, vk_alloc, &cpu_tex, up_pool,
                               tex_pool, format, t, true);
  TracyCZoneEnd(prof_e)
  return err;
}

int32_t create_texture(VkDevice device, VmaAllocator vma_alloc,
                       const VkAllocationCallbacks *vk_alloc,
                       const CPUTexture *tex, VmaPool up_pool, VmaPool tex_pool,
                       VkFormat format, GPUTexture *t, bool gen_mips) {
  TracyCZoneN(prof_e, "create_texture", true)
  VkResult err = VK_SUCCESS;

  VkDeviceSize host_buffer_size = tex->data_size;
  uint32_t layer_count = tex->layer_count;
  uint32_t mip_count = tex->mip_count;
  const TextureMip *tex_mip = &tex->layers[0].mips[0];
  uint32_t img_width = tex_mip->width;
  uint32_t img_height = tex_mip->height;

  // Allocate host buffer for image data
  GPUBuffer host_buffer = {0};
  {
    VkBufferCreateInfo buffer_create_info = {0};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = host_buffer_size;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_create_info.pool = up_pool;
    VmaAllocationInfo alloc_info = {0};
    err = vmaCreateBuffer(vma_alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }

  uint32_t desired_mip_levels = mip_count;
  if (gen_mips) {
    desired_mip_levels = (uint32_t)(floorf(log2f((float)(SDL_max(img_width, img_height))))) + 1;
  }

  // Allocate device image
  GPUImage device_image = {0};
  {
    VkImageUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo img_info = {0};
    img_info.flags = 0;
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = format;
    img_info.extent = (VkExtent3D){img_width, img_height, 1};
    img_info.mipLevels = desired_mip_levels;
    img_info.arrayLayers = layer_count;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(vma_alloc, &img_info, &alloc_info, &device_image);
    assert(err == VK_SUCCESS);
  }

  // Create Image View
  VkImageView view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = device_image.image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = format;
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, desired_mip_levels, 0, layer_count};
    err = vkCreateImageView(device, &create_info, vk_alloc, &view);
    assert(err == VK_SUCCESS);
  }

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);

    uint64_t data_size = tex->data_size;
    memcpy(data, tex->data, data_size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }

  t->host = host_buffer;
  t->device = device_image;
  t->format = (uint32_t)format;
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = desired_mip_levels;
  t->gen_mips = gen_mips;
  t->layer_count = tex->layer_count;
  t->view = view;
  t->region_count = 1;
  t->regions[0] = (VkBufferImageCopy){
      .imageExtent =
          {
              .width = img_width,
              .height = img_height,
              .depth = 1,
          },
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = tex->layer_count,
          },
  };

  TracyCZoneEnd(prof_e)
  return err;
}

void destroy_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const GPUTexture *t) {
  destroy_gpubuffer(vma_alloc, &t->host);
  destroy_gpuimage(vma_alloc, &t->device);
  vkDestroyImageView(device, t->view, vk_alloc);
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
#endif
static GPUPipeline *alloc_gpupipeline(Allocator alloc, uint32_t perm_count) {
  size_t pipe_handles_size = sizeof(VkPipeline) * perm_count;
  size_t feature_flags_size = sizeof(uint64_t) * perm_count;
  size_t input_flags_size = sizeof(uint64_t) * perm_count;
  size_t pipeline_size = sizeof(GPUPipeline);
  size_t alloc_size =
      pipeline_size + pipe_handles_size + feature_flags_size + input_flags_size;
  GPUPipeline *p = (GPUPipeline *)hb_alloc(alloc, alloc_size);
  uint8_t *mem = (uint8_t *)p;
  assert(p);
  p->pipeline_count = perm_count;

  size_t offset = pipeline_size;

  p->pipeline_flags = (uint64_t *)(mem + offset);
  offset += feature_flags_size;

  p->input_flags = (uint64_t *)(mem + offset);
  offset += input_flags_size;

  p->pipelines = (VkPipeline *)(mem + offset);
  // unnecessary write - offset += pipe_handles_size;

  return p;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

int32_t create_gfx_pipeline(const GPUPipelineDesc *desc, GPUPipeline **p) {
  TracyCZoneN(prof_e, "create_gfx_pipeline", true)

  uint32_t total_perm_count = desc->feature_perm_count * desc->input_perm_count;

  GPUPipeline *pipe = alloc_gpupipeline(desc->std_alloc, total_perm_count);
  VkResult err = VK_SUCCESS;

  VkGraphicsPipelineCreateInfo *pipe_create_info = hb_alloc_nm_tp(
      desc->tmp_alloc, total_perm_count, VkGraphicsPipelineCreateInfo);
  assert(pipe_create_info);

  uint32_t perm_idx = 0;

  for (uint32_t i = 0; i < desc->input_perm_count; ++i) {
    const VkGraphicsPipelineCreateInfo *info_base = &desc->create_info_bases[i];

    // Calculate this base's input permutation
    uint64_t input_perm = 0;
    for (uint32_t ii = 0;
         ii < info_base->pVertexInputState->vertexAttributeDescriptionCount;
         ++ii) {
      const VkVertexInputAttributeDescription attr_desc =
          info_base->pVertexInputState->pVertexAttributeDescriptions[ii];
      if (attr_desc.binding == 0 &&
          attr_desc.format == VK_FORMAT_R32G32B32_SFLOAT) {
        input_perm |= VA_INPUT_PERM_POSITION;
      } else if (attr_desc.binding == 1 &&
                 attr_desc.format == VK_FORMAT_R32G32B32_SFLOAT) {
        input_perm |= VA_INPUT_PERM_NORMAL;
      } else if (attr_desc.binding == 2 &&
                 attr_desc.format == VK_FORMAT_R32G32B32A32_SFLOAT) {
        input_perm |= VA_INPUT_PERM_TANGENT;
      } else if ((attr_desc.binding == 2 &&
                  attr_desc.format == VK_FORMAT_R32G32_SFLOAT) ||
                 (attr_desc.binding == 3 &&
                  attr_desc.format == VK_FORMAT_R32G32_SFLOAT)) {
        input_perm |= VA_INPUT_PERM_TEXCOORD0;
      } else if (attr_desc.binding == 3 &&
                 attr_desc.format == VK_FORMAT_R32G32_SFLOAT) {
        input_perm |= VA_INPUT_PERM_TEXCOORD1;
      } else {
        SDL_assert(false);
      }
      pipe->input_flags[i] = input_perm;
    }

    uint32_t stage_count = info_base->stageCount;
    uint32_t perm_stage_count = desc->feature_perm_count * stage_count;

    // Every shader stage needs its own create info
    VkPipelineShaderStageCreateInfo *pipe_stage_info = hb_alloc_nm_tp(
        desc->tmp_alloc, perm_stage_count, VkPipelineShaderStageCreateInfo);

    VkSpecializationMapEntry map_entries[1] = {
        {0, 0, sizeof(uint32_t)},
    };

    VkSpecializationInfo *spec_info = hb_alloc_nm_tp(
        desc->tmp_alloc, desc->feature_perm_count, VkSpecializationInfo);

    uint32_t *flags =
        hb_alloc_nm_tp(desc->tmp_alloc, desc->feature_perm_count, uint32_t);

    // Insert specialization info to every shader stage
    for (uint32_t ii = 0; ii < desc->feature_perm_count; ++ii) {

      pipe_create_info[perm_idx] = *info_base;

      flags[ii] = ii;
      spec_info[ii] = (VkSpecializationInfo){
          1,
          map_entries,
          sizeof(uint32_t),
          &flags[ii],
      };

      uint32_t stage_idx = ii * stage_count;
      for (uint32_t iii = 0; iii < stage_count; ++iii) {
        VkPipelineShaderStageCreateInfo *stage =
            &pipe_stage_info[stage_idx + iii];
        *stage = info_base->pStages[iii];
        stage->pSpecializationInfo = &spec_info[ii];
      }
      pipe_create_info[perm_idx].pStages = &pipe_stage_info[stage_idx];

      // Set permutation tracking values
      pipe->input_flags[perm_idx] = input_perm;
      pipe->pipeline_flags[perm_idx] = ii;
      perm_idx++;
    }
  }

  err = vkCreateGraphicsPipelines(desc->device, desc->cache, total_perm_count,
                                  pipe_create_info, desc->vk_alloc,
                                  pipe->pipelines);
  assert(err == VK_SUCCESS);

  *p = pipe;
  TracyCZoneEnd(prof_e)
  return err;
}

int32_t create_rt_pipeline(
    VkDevice device, const VkAllocationCallbacks *vk_alloc, Allocator tmp_alloc,
    Allocator std_alloc, VkPipelineCache cache,
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelines,
    uint32_t perm_count, VkRayTracingPipelineCreateInfoKHR *create_info_base,
    GPUPipeline **p) {
  TracyCZoneN(prof_e, "create_rt_pipeline", true)
  GPUPipeline *pipe = alloc_gpupipeline(std_alloc, perm_count);
  VkResult err = VK_SUCCESS;

  VkRayTracingPipelineCreateInfoKHR *pipe_create_info =
      hb_alloc_nm_tp(tmp_alloc, perm_count, VkRayTracingPipelineCreateInfoKHR);
  assert(pipe_create_info);

  uint32_t stage_count = create_info_base->stageCount;
  uint32_t perm_stage_count = perm_count * stage_count;

  // Every shader stage needs its own create info
  VkPipelineShaderStageCreateInfo *pipe_stage_info = hb_alloc_nm_tp(
      tmp_alloc, perm_stage_count, VkPipelineShaderStageCreateInfo);
  VkSpecializationMapEntry map_entries[1] = {
      {0, 0, sizeof(uint32_t)},
  };

  VkSpecializationInfo *spec_info =
      hb_alloc_nm_tp(tmp_alloc, perm_count, VkSpecializationInfo);
  uint32_t *flags = hb_alloc_nm_tp(tmp_alloc, perm_count, uint32_t);

  // Insert specialization info to every shader stage
  for (uint32_t i = 0; i < perm_count; ++i) {
    pipe_create_info[i] = *create_info_base;

    flags[i] = i;
    spec_info[i] = (VkSpecializationInfo){
        1,
        map_entries,
        sizeof(uint32_t),
        &flags[i],
    };

    uint32_t stage_idx = i * stage_count;
    for (uint32_t ii = 0; ii < stage_count; ++ii) {
      VkPipelineShaderStageCreateInfo *stage = &pipe_stage_info[stage_idx + ii];
      *stage = create_info_base->pStages[ii];
      stage->pSpecializationInfo = &spec_info[i];
    }
    pipe_create_info[i].pStages = &pipe_stage_info[stage_idx];
  }

  err =
      vkCreateRayTracingPipelines(device, VK_NULL_HANDLE, cache, perm_count,
                                  pipe_create_info, vk_alloc, pipe->pipelines);
  assert(err == VK_SUCCESS);

  hb_free(tmp_alloc, pipe_create_info);
  hb_free(tmp_alloc, pipe_stage_info);
  hb_free(tmp_alloc, spec_info);
  hb_free(tmp_alloc, flags);

  *p = pipe;
  TracyCZoneEnd(prof_e)
  return err;
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
void destroy_gpupipeline(VkDevice device, Allocator alloc,
                         const VkAllocationCallbacks *vk_alloc,
                         const GPUPipeline *p) {
  for (uint32_t i = 0; i < p->pipeline_count; ++i) {
    vkDestroyPipeline(device, p->pipelines[i], vk_alloc);
  }

  hb_free(alloc, (void *)p);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

uint32_t collect_material_textures(uint32_t tex_count,
                                   const cgltf_texture *gltf_textures,
                                   const cgltf_material *material,
                                   uint32_t tex_idx_start,
                                   uint32_t *mat_tex_refs) {
  uint32_t tex_idx = 0;
  uint32_t tex_ref_count = 0;
  for (uint32_t i = 0; i < tex_count; ++i) {
    const cgltf_texture *tex = &gltf_textures[i];
    // Standard textures
    if (material->normal_texture.texture != NULL) {
      if (tex == material->normal_texture.texture) {
        mat_tex_refs[1] = tex_idx_start + i;
        tex_ref_count++;
      }
    }
    if (material->emissive_texture.texture != NULL) {
      if (tex == material->emissive_texture.texture) {
        mat_tex_refs[4] = tex_idx_start + i;
        tex_ref_count++;
      }
    }
    if (material->occlusion_texture.texture != NULL) {
      if (tex == material->occlusion_texture.texture) {
        mat_tex_refs[5] = tex_idx_start + i;
        tex_ref_count++;
      }
    }

    // Specifics
    if (material->has_pbr_metallic_roughness) {
      if (material->pbr_metallic_roughness.base_color_texture.texture != NULL) {
        if (tex ==
            material->pbr_metallic_roughness.base_color_texture.texture) {
          mat_tex_refs[0] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Metallic Roughness but no base color texture "
            "was provided");
        SDL_assert(false);
      }
      if (material->pbr_metallic_roughness.metallic_roughness_texture.texture !=
          NULL) {
        if (tex == material->pbr_metallic_roughness.metallic_roughness_texture
                       .texture) {
          mat_tex_refs[2] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Metallic Roughness but no metallic roughness "
            "texture was provided");
        SDL_assert(false);
      }
    }
    if (material->has_pbr_specular_glossiness) {
      if (material->pbr_specular_glossiness.diffuse_texture.texture != NULL) {
        if (tex == material->pbr_specular_glossiness.diffuse_texture.texture) {
          mat_tex_refs[0] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Specular Glossiness but no diffuse texture "
            "was provided");
        SDL_assert(false);
      }
      if (material->pbr_specular_glossiness.specular_glossiness_texture
              .texture != NULL) {
        if (tex == material->pbr_specular_glossiness.specular_glossiness_texture
                       .texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
                     "Material has Specular Glossiness but no specular "
                     "glossiness texture was provided");
        SDL_assert(false);
      }
    }
    if (material->has_clearcoat) {
      if (material->clearcoat.clearcoat_texture.texture != NULL) {
        if (tex == material->clearcoat.clearcoat_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Clearcoat but no clearcoat texture was provided");
        SDL_assert(false);
      }
      if (material->clearcoat.clearcoat_roughness_texture.texture != NULL) {
        if (tex == material->clearcoat.clearcoat_roughness_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Clearcoat but no roughness texture was provided");
        SDL_assert(false);
      }
      if (material->clearcoat.clearcoat_normal_texture.texture != NULL) {
        if (tex == material->clearcoat.clearcoat_normal_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Clearcoat but no normal texture was provided");
        SDL_assert(false);
      }
    }
    if (material->has_transmission) {
      if (material->transmission.transmission_texture.texture != NULL) {
        if (tex == material->transmission.transmission_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Clearcoat but no normal texture was provided");
        SDL_assert(false);
      }
    }
    if (material->has_volume) {
      if (material->volume.thickness_texture.texture != NULL) {
        if (tex == material->volume.thickness_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Clearcoat but no normal texture was provided");
        SDL_assert(false);
      }
    }
    if (material->has_specular) {
      if (material->specular.specular_texture.texture != NULL) {
        if (tex == material->specular.specular_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Specular but no specular texture was provided");
        SDL_assert(false);
      }
      if (material->specular.specular_color_texture.texture != NULL) {
        if (tex == material->specular.specular_color_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Specular but no color texture was provided");
        SDL_assert(false);
      }
    }
    if (material->has_sheen) {
      if (material->sheen.sheen_color_texture.texture != NULL) {
        if (tex == material->sheen.sheen_color_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Sheen but no color texture was provided");
        SDL_assert(false);
      }
      if (material->sheen.sheen_roughness_texture.texture != NULL) {
        if (tex == material->sheen.sheen_roughness_texture.texture) {
          mat_tex_refs[tex_idx++] = tex_idx_start + i;
          tex_ref_count++;
        }
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s",
            "Material has Sheen but no roughness texture was provided");
        SDL_assert(false);
      }
    }

    // TODO: Extensions
  }
  return tex_ref_count;
}

int32_t create_gpumaterial_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                 const VkAllocationCallbacks *vk_alloc,
                                 const cgltf_material *gltf, uint32_t tex_count,
                                 uint32_t *tex_refs, GPUMaterial *m) {
  TracyCZoneN(prof_e, "create_gpumaterial_cgltf", true)
  VkResult err = VK_SUCCESS;

  // Convert from cgltf structs to our struct
  uint64_t feat_perm = 0;
  GLTFMaterialData mat_data = {0};
  {
    memcpy(&mat_data.pbr_metallic_roughness.base_color_factor,
           gltf->pbr_metallic_roughness.base_color_factor, sizeof(float) * 4);
    mat_data.pbr_metallic_roughness.metallic_factor =
        gltf->pbr_metallic_roughness.metallic_factor;
    mat_data.pbr_metallic_roughness.roughness_factor =
        gltf->pbr_metallic_roughness.roughness_factor;

    memcpy(&mat_data.pbr_specular_glossiness.diffuse_factor,
           gltf->pbr_specular_glossiness.diffuse_factor, sizeof(float) * 4);
    memcpy(&mat_data.pbr_specular_glossiness.specular_factor,
           gltf->pbr_specular_glossiness.specular_factor, sizeof(float) * 3);
    mat_data.pbr_specular_glossiness.glossiness_factor =
        gltf->pbr_specular_glossiness.glossiness_factor;

    mat_data.clearcoat_factor = gltf->clearcoat.clearcoat_factor;
    mat_data.clearcoat_roughness_factor =
        gltf->clearcoat.clearcoat_roughness_factor;

    mat_data.ior = gltf->ior.ior;

    memcpy(&mat_data.specular.color_factor,
           gltf->specular.specular_color_factor, sizeof(float) * 3);
    mat_data.specular.specular_factor = gltf->specular.specular_factor;

    memcpy(&mat_data.sheen.color_factor, gltf->sheen.sheen_color_factor,
           sizeof(float) * 3);
    mat_data.sheen.roughness_factor = gltf->sheen.sheen_roughness_factor;

    mat_data.transmission_factor = gltf->transmission.transmission_factor;

    mat_data.volume.thickness_factor = gltf->volume.thickness_factor;
    memcpy(&mat_data.volume.attenuation_color, gltf->volume.attenuation_color,
           sizeof(float) * 3);
    mat_data.volume.attenuation_distance = gltf->volume.attenuation_distance;
  }

  // Determine feature permutation
  if (gltf->has_pbr_metallic_roughness) {
    feat_perm |= GLTF_PERM_PBR_METALLIC_ROUGHNESS;
    if (gltf->pbr_metallic_roughness.metallic_roughness_texture.texture !=
        NULL) {
      feat_perm |= GLTF_PERM_PBR_METAL_ROUGH_TEX;
    }
    if (gltf->pbr_metallic_roughness.base_color_texture.texture != NULL) {
      feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
    }
  }
  if (gltf->has_pbr_specular_glossiness) {
    feat_perm |= GLTF_PERM_PBR_SPECULAR_GLOSSINESS;
    if (gltf->pbr_specular_glossiness.diffuse_texture.texture != NULL) {
      feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
    }
  }
  if (gltf->has_clearcoat) {
    feat_perm |= GLTF_PERM_CLEARCOAT;
  }
  if (gltf->has_transmission) {
    feat_perm |= GLTF_PERM_TRANSMISSION;
  }
  if (gltf->has_volume) {
    feat_perm |= GLTF_PERM_VOLUME;
  }
  if (gltf->has_ior) {
    feat_perm |= GLTF_PERM_IOR;
  }
  if (gltf->has_specular) {
    feat_perm |= GLTF_PERM_SPECULAR;
  }
  if (gltf->has_sheen) {
    feat_perm |= GLTF_PERM_SHEEN;
  }
  if (gltf->unlit) {
    feat_perm |= GLTF_PERM_UNLIT;
  }
  if (gltf->normal_texture.texture != NULL) {
    feat_perm |= GLTF_PERM_NORMAL_MAP;
  }

  m->feature_perm = feat_perm;
  m->texture_count = tex_count;
  memcpy(m->texture_refs, tex_refs, sizeof(uint32_t) * MAX_MATERIAL_TEXTURES);

  // Create host buffer for material data
  m->const_buffer = create_gpuconstbuffer(device, vma_alloc, vk_alloc,
                                          sizeof(GLTFMaterialData));
  {
    uint8_t *data = NULL;
    VmaAllocation alloc = m->const_buffer.host.alloc;
    vmaMapMemory(vma_alloc, alloc, (void **)&data);

    // Copy Data
    memcpy(data, &mat_data, sizeof(GLTFMaterialData));

    vmaUnmapMemory(vma_alloc, alloc);
  }

  TracyCZoneEnd(prof_e)
  return err;
}
void destroy_material(VkDevice device, VmaAllocator vma_alloc,
                      const VkAllocationCallbacks *vk_alloc,
                      const GPUMaterial *m) {
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, m->const_buffer);
}
