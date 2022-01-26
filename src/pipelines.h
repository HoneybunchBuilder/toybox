#pragma once

#include <stdint.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "allocator.h"

typedef struct GPUPipeline GPUPipeline;

uint32_t create_fractal_pipeline(VkDevice device,
                                 const VkAllocationCallbacks *vk_alloc,
                                 VkPipelineCache cache, VkRenderPass pass,
                                 uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe);

uint32_t create_color_mesh_pipeline(VkDevice device,
                                    const VkAllocationCallbacks *vk_alloc,
                                    VkPipelineCache cache, VkRenderPass pass,
                                    uint32_t w, uint32_t h,
                                    VkPipelineLayout layout, VkPipeline *pipe);

uint32_t create_uv_mesh_pipeline(VkDevice device,
                                 const VkAllocationCallbacks *vk_alloc,
                                 VkPipelineCache cache, VkRenderPass pass,
                                 uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe);

uint32_t create_skydome_pipeline(VkDevice device,
                                 const VkAllocationCallbacks *vk_alloc,
                                 VkPipelineCache cache, VkRenderPass pass,
                                 uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe);

uint32_t create_imgui_pipeline(VkDevice device,
                               const VkAllocationCallbacks *vk_alloc,
                               VkPipelineCache cache, VkRenderPass pass,
                               uint32_t w, uint32_t h, VkPipelineLayout layout,
                               VkPipeline *pipe);

uint32_t create_shadow_pipeline(VkDevice device,
                                const VkAllocationCallbacks *vk_alloc,
                                VkPipelineCache cache, VkRenderPass pass,
                                uint32_t w, uint32_t h, VkPipelineLayout layout,
                                VkPipeline *pipe);

enum GLTF_PERMUTATIONS {
  GLTF_PERM_NONE = 0x00000000,
  GLTF_PERM_NORMAL_MAP = 0x00000001,
  GLTF_PERM_PBR_METALLIC_ROUGHNESS = 0x00000002,
  GLTF_PERM_PBR_METAL_ROUGH_TEX = 0x00000004,
  GLTF_PERM_PBR_SPECULAR_GLOSSINESS = 0x0000008,
  GLTF_PERM_CLEARCOAT = 0x00000010,
  GLTF_PERM_TRANSMISSION = 0x00000020,
  GLTF_PERM_VOLUME = 0x00000040,
  GLTF_PERM_IOR = 0x00000080,
  GLTF_PERM_SPECULAR = 0x00000100,
  GLTF_PERM_SHEEN = 0x000000200,
  GLTF_PERM_UNLIT = 0x00000400,
  // Actually 12... but using 3 to cut down on startup time
  GLTF_PERM_FLAG_COUNT = 3,
};

uint32_t create_gltf_pipeline(VkDevice device,
                              const VkAllocationCallbacks *vk_alloc,
                              Allocator tmp_alloc, Allocator std_alloc,
                              VkPipelineCache cache, VkRenderPass pass,
                              uint32_t w, uint32_t h, VkPipelineLayout layout,
                              GPUPipeline **pipe);

uint32_t create_gltf_rt_pipeline(
    VkDevice device, const VkAllocationCallbacks *vk_alloc, Allocator tmp_alloc,
    Allocator std_alloc, VkPipelineCache cache,
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelines,
    VkPipelineLayout layout, GPUPipeline **pipe);
