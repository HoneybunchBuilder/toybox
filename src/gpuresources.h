#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "allocator.h"

typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;
typedef struct VmaPool_T *VmaPool;

typedef struct VmaAllocationCreateInfo VmaAllocationCreateInfo;

typedef struct Allocator Allocator;
typedef struct CPUMesh CPUMesh;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct CPUTexture CPUTexture;
typedef struct cgltf_texture cgltf_texture;
typedef struct cgltf_material cgltf_material;

typedef struct GPUBuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} GPUBuffer;

typedef struct GPUConstBuffer {
  size_t size;
  GPUBuffer host;
  GPUBuffer gpu;
  VkSemaphore updated;
} GPUConstBuffer;

typedef struct GPUSurface {
  uint64_t input_perm;
  size_t idx_count;
  size_t vtx_count;
  int32_t idx_type;
  size_t size;
  size_t idx_size;
  size_t vtx_size;
  GPUBuffer host;
  GPUBuffer gpu;
} GPUSurface;

#define MAX_SURFACE_COUNT 32

typedef struct GPUMesh {
  uint32_t surface_count;
  GPUSurface surfaces[MAX_SURFACE_COUNT];
} GPUMesh;

typedef struct GPUImage {
  VkImage image;
  VmaAllocation alloc;
} GPUImage;

#define MAX_REGION_COUNT 16

typedef struct GPUTexture {
  GPUBuffer host;
  GPUImage device;
  VkImageView view;
  uint32_t width;
  uint32_t height;
  uint32_t mip_levels;
  bool gen_mips;
  uint32_t layer_count;
  uint32_t format;
  uint32_t region_count;
  VkBufferImageCopy regions[MAX_REGION_COUNT];
} GPUTexture;

typedef struct GPUPipelineDesc {
  VkDevice device;
  const VkAllocationCallbacks *vk_alloc;
  Allocator tmp_alloc;
  Allocator std_alloc;
  VkPipelineCache cache;

  uint32_t feature_perm_count;
  uint32_t input_perm_count;
  // One info struct per input permutation
  VkGraphicsPipelineCreateInfo *create_info_bases;
} GPUPipelineDesc;

typedef struct GPUPipeline {
  uint32_t pipeline_id;
  uint32_t pipeline_count;
  uint64_t *input_flags;
  uint64_t *pipeline_flags;
  // Collection of pipelines per vertex input and per featureset
  VkPipeline *pipelines;
} GPUPipeline;

#define MAX_MATERIAL_TEXTURES 8

/*
  TODO: Figure out how to best represent materials
  For now I'm imagining that a material will be able to be implemented
  mostly with 1 uniform buffer for parameters and up to 8 textures for
  other resource bindings.
*/
typedef struct GPUMaterial {
  uint64_t feature_perm;
  // All material parameters go into one uniform buffer
  // The uniform buffer takes up location 0
  GPUConstBuffer const_buffer;

  // textures take up locations 1-8 in the descriptor set
  uint32_t texture_count;
  uint32_t texture_refs[MAX_MATERIAL_TEXTURES];
} GPUMaterial;

int32_t create_gpubuffer(VmaAllocator allocator, uint64_t size,
                         int32_t mem_usage, int32_t buf_usage, GPUBuffer *out);
void destroy_gpubuffer(VmaAllocator allocator, const GPUBuffer *buffer);

GPUConstBuffer create_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                                     const VkAllocationCallbacks *vk_alloc,
                                     uint64_t size);
GPUConstBuffer create_gpustoragebuffer(VkDevice device, VmaAllocator allocator,
                                       const VkAllocationCallbacks *vk_alloc,
                                       uint64_t size);
void destroy_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                            const VkAllocationCallbacks *vk_alloc,
                            GPUConstBuffer cb);

int32_t create_gpumesh(VmaAllocator vma_alloc, uint64_t input_perm,
                       const CPUMesh *src_mesh, GPUMesh *dst_mesh);
int32_t create_gpumesh_cgltf(VkDevice device, VmaAllocator vma_alloc,
                             Allocator tmp_alloc, const cgltf_mesh *src_mesh,
                             GPUMesh *dst_mesh);
void destroy_gpumesh(VmaAllocator vma_alloc, const GPUMesh *mesh);

int32_t create_gpuimage(VmaAllocator vma_alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,

                        GPUImage *i);
void destroy_gpuimage(VmaAllocator allocator, const GPUImage *image);

GPUTexture load_ktx2_texture(VkDevice device, VmaAllocator vma_alloc,
                             Allocator *tmp_alloc,
                             const VkAllocationCallbacks *vk_alloc,
                             const char *file_path, VmaPool up_pool,
                             VmaPool tex_pool);

int32_t load_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const char *filename, VmaPool up_pool, VmaPool tex_pool,
                     GPUTexture *t);
int32_t create_texture(VkDevice device, VmaAllocator vma_alloc,
                       const VkAllocationCallbacks *vk_alloc,
                       const CPUTexture *tex, VmaPool up_pool, VmaPool tex_pool,
                       GPUTexture *t, bool gen_mips);
int32_t create_gputexture_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                const VkAllocationCallbacks *vk_alloc,
                                const cgltf_texture *gltf, const uint8_t *bin,
                                VmaPool up_pool, VmaPool tex_pool,
                                GPUTexture *t);
void destroy_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const GPUTexture *t);

int32_t create_gfx_pipeline(const GPUPipelineDesc *desc, GPUPipeline **p);
int32_t create_rt_pipeline(
    VkDevice device, const VkAllocationCallbacks *vk_alloc, Allocator tmp_alloc,
    Allocator std_alloc, VkPipelineCache cache,
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelines,
    uint32_t perm_count, VkRayTracingPipelineCreateInfoKHR *create_info_base,
    GPUPipeline **p);
void destroy_gpupipeline(VkDevice device, Allocator alloc,
                         const VkAllocationCallbacks *vk_alloc,
                         const GPUPipeline *p);

uint32_t collect_material_textures(uint32_t tex_count,
                                   const cgltf_texture *gltf_textures,
                                   const cgltf_material *material,
                                   uint32_t tex_idx_start,
                                   uint32_t *mat_tex_refs);

int32_t create_gpumaterial_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                 const VkAllocationCallbacks *vk_alloc,
                                 const cgltf_material *gltf, uint32_t tex_count,
                                 uint32_t *tex_refs, GPUMaterial *m);
void destroy_material(VkDevice device, VmaAllocator vma_alloc,
                      const VkAllocationCallbacks *vk_alloc,
                      const GPUMaterial *m);
