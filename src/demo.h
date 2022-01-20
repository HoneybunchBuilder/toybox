#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#undef VK_NO_PROTOTYPES

#include "allocator.h"
#include "gpuresources.h"
#include "profiling.h"
#include "scene.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#ifdef __ANDROID__
#define FRAME_LATENCY 4
#else
#define FRAME_LATENCY 3
#endif
#define CONST_BUFFER_UPLOAD_QUEUE_SIZE 2048
#define MESH_UPLOAD_QUEUE_SIZE 2048
#define TEXTURE_UPLOAD_QUEUE_SIZE 2048

typedef union SDL_Event SDL_Event;
typedef struct SDL_Window SDL_Window;

typedef struct SwapchainInfo {
  bool valid;
  uint32_t image_count;
  VkFormat format;
  VkColorSpaceKHR color_space;
  VkPresentModeKHR present_mode;
  uint32_t width;
  uint32_t height;
} SwapchainInfo;

#define MAX_DESCRIPTOR_TYPES 14
typedef struct DynPoolState {
  uint32_t pool_sizes[MAX_DESCRIPTOR_TYPES];
  uint32_t max_set_count;
} DynPoolState;

#define CONST_BUFFER_BLOCK_SIZE 8

typedef struct Demo {
  Allocator std_alloc;
  Allocator tmp_alloc;

  SDL_Window *window;

  const VkAllocationCallbacks *vk_alloc;
  VmaAllocator vma_alloc;

  VkInstance instance;

  VkPhysicalDevice gpu;
  VkPhysicalDeviceProperties gpu_props;
  uint32_t queue_family_count;
  VkQueueFamilyProperties *queue_props;
  VkPhysicalDeviceFeatures gpu_features;
  VkPhysicalDeviceMemoryProperties gpu_mem_props;

  VkSurfaceKHR surface;
  uint32_t graphics_queue_family_index;
  uint32_t present_queue_family_index;
  bool separate_present_queue;

  VkDevice device;
  VkQueue present_queue;
  VkQueue graphics_queue;

  SwapchainInfo swap_info;
  VkSwapchainKHR swapchain;

  VkRenderPass render_pass;
  VkRenderPass imgui_pass;

  VkPipelineCache pipeline_cache;

  VkSampler sampler;

  VkDescriptorSetLayout skydome_layout;
  VkDescriptorSetLayout hosek_layout;
  VkPipelineLayout skydome_pipe_layout;
  VkPipeline skydome_pipeline;
  GPUConstBuffer sky_const_buffer;
  GPUConstBuffer hosek_const_buffer;
  GPUConstBuffer camera_const_buffer;
  GPUConstBuffer light_const_buffer;

  uint32_t obj_const_buffer_block_count;
  GPUConstBuffer **obj_const_buffer_blocks;

  VkDescriptorSetLayout gltf_material_set_layout;
  VkDescriptorSetLayout gltf_object_set_layout;
  VkDescriptorSetLayout gltf_view_set_layout;
  VkPipelineLayout gltf_pipe_layout;
  GPUPipeline *gltf_pipeline;

  VkDescriptorSetLayout gltf_rt_layout;
  VkPipelineLayout gltf_rt_pipe_layout;
  GPUPipeline *gltf_rt_pipeline;

  VkDescriptorSetLayout imgui_layout;
  VkPipelineLayout imgui_pipe_layout;
  VkPipeline imgui_pipeline;

  VkImage swapchain_images[FRAME_LATENCY];
  VkImageView swapchain_image_views[FRAME_LATENCY];
  VkFramebuffer main_pass_framebuffers[FRAME_LATENCY];
  VkFramebuffer ui_pass_framebuffers[FRAME_LATENCY];

  GPUImage depth_buffers; // Implemented as an image array; one image for each
                          // latency frame
  VkImageView depth_buffer_views[FRAME_LATENCY];

  VkCommandPool command_pools[FRAME_LATENCY];
  VkCommandBuffer upload_buffers[FRAME_LATENCY];
  VkCommandBuffer graphics_buffers[FRAME_LATENCY];
  VkCommandBuffer screenshot_buffers[FRAME_LATENCY];

  TracyCGPUContext *tracy_gpu_contexts[FRAME_LATENCY];

  // For allowing the currently processed frame to access
  // resources being uploaded this frame
  VkSemaphore upload_complete_sems[FRAME_LATENCY];
  VkSemaphore img_acquired_sems[FRAME_LATENCY];
  VkSemaphore swapchain_image_sems[FRAME_LATENCY];
  VkSemaphore render_complete_sems[FRAME_LATENCY];

  uint32_t frame_idx;
  uint32_t swap_img_idx;
  VkFence fences[FRAME_LATENCY];

  VmaPool upload_mem_pool;
  VmaPool texture_mem_pool;

  GPUMesh skydome_gpu;

  size_t imgui_mesh_data_size[FRAME_LATENCY];
  uint8_t *imgui_mesh_data;
  GPUMesh imgui_gpu[FRAME_LATENCY];
  GPUTexture imgui_atlas;

  Scene *duck_scene;
  Scene *floor_scene;
  Scene *main_scene;

  GPUImage screenshot_image;
  VkFence screenshot_fence;

  DynPoolState dyn_desc_pool_states[FRAME_LATENCY];
  VkDescriptorPool dyn_desc_pools[FRAME_LATENCY];
  VkDescriptorPool descriptor_pools[FRAME_LATENCY];
  VkDescriptorSet skydome_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet hosek_descriptor_set;
  VkDescriptorSet gltf_view_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet imgui_descriptor_sets[FRAME_LATENCY];

  uint32_t const_buffer_upload_count;
  GPUConstBuffer *const_buffer_upload_queue;

  uint32_t mesh_upload_count;
  GPUMesh *mesh_upload_queue;

  uint32_t texture_upload_count;
  GPUTexture *texture_upload_queue;

  ImGuiContext *ig_ctx;
  ImGuiIO *ig_io;
} Demo;

bool demo_init(SDL_Window *window, VkInstance instance, Allocator std_alloc,
               Allocator tmp_alloc, const VkAllocationCallbacks *vk_alloc,
               Demo *d);
void demo_destroy(Demo *d);

void demo_upload_const_buffer(Demo *d, const GPUConstBuffer *buffer);
void demo_upload_mesh(Demo *d, const GPUMesh *mesh);
void demo_upload_texture(Demo *d, const GPUTexture *tex);
void demo_upload_scene(Demo *d, const Scene *s);

void demo_process_event(Demo *d, const SDL_Event *e);
void demo_render_frame(Demo *d, const float4x4 *vp, const float4x4 *sky_vp);

bool demo_screenshot(Demo *d, Allocator std_alloc, uint8_t **screenshot_bytes,
                     uint32_t *screenshot_size);
