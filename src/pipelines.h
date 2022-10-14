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

uint32_t create_env_filter_pipeline(VkDevice device,
                                    const VkAllocationCallbacks *vk_alloc,
                                    VkPipelineCache cache, VkRenderPass pass,
                                    uint32_t w, uint32_t h,
                                    VkPipelineLayout layout, VkPipeline *pipe);

uint32_t create_brdf_pipeline(VkDevice device,
                              const VkAllocationCallbacks *vk_alloc,
                              VkPipelineCache cache, VkRenderPass pass,
                              uint32_t w, uint32_t h, VkPipelineLayout layout,
                              VkPipeline *pipe);

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
