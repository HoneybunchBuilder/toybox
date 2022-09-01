#include "rendersystem.h"

#include "mimalloc.h"
#include "profiling.h"
#include "renderthread.h"
#include "tbcommon.h"
#include "tbvma.h"
#include "vkdbg.h"
#include "world.h"

bool create_render_system(RenderSystem *self,
                          const RenderSystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  TB_CHECK_RETURN(desc, "Invalid RenderSystemDescriptor", false);
  RenderThread *thread = desc->render_thread;
  TB_CHECK_RETURN(thread, "Invalid RenderThread", false);
  *self = (RenderSystem){
      .render_thread = thread,
  };

  // Should be safe to assume that the render thread is initialized by now
  {
    VkResult err = VK_SUCCESS;

    // Create vulkan allocator for main thread
    self->vk_host_alloc = (VkHostAlloc){
        .name = "Vulkan Main Thread Temp Host",
        .heap = mi_heap_new(),
    };
    TB_CHECK_RETURN(self->vk_host_alloc.heap, "Failed to create main vk heap",
                    false);

    self->vk_host_alloc_cb = (VkAllocationCallbacks){
        .pUserData = &self->vk_host_alloc,
        .pfnAllocation = tb_vk_alloc_fn,
        .pfnReallocation = tb_vk_realloc_fn,
        .pfnFree = tb_vk_free_fn,
    };

    // Create vma allocator for main thread
    {
      VmaVulkanFunctions volk_functions = {
          .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
          .vkGetPhysicalDeviceMemoryProperties =
              vkGetPhysicalDeviceMemoryProperties,
          .vkAllocateMemory = vkAllocateMemory,
          .vkFreeMemory = vkFreeMemory,
          .vkMapMemory = vkMapMemory,
          .vkUnmapMemory = vkUnmapMemory,
          .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
          .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
          .vkBindBufferMemory = vkBindBufferMemory,
          .vkBindImageMemory = vkBindImageMemory,
          .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
          .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
          .vkCreateBuffer = vkCreateBuffer,
          .vkDestroyBuffer = vkDestroyBuffer,
          .vkCreateImage = vkCreateImage,
          .vkDestroyImage = vkDestroyImage,
          .vkCmdCopyBuffer = vkCmdCopyBuffer,
      };
      VmaDeviceMemoryCallbacks vma_callbacks = {
          tb_vma_alloc_fn,
          tb_vma_free_fn,
          NULL,
      };
      VmaAllocatorCreateInfo create_info = {
          .physicalDevice = thread->gpu,
          .device = thread->device,
          .pVulkanFunctions = &volk_functions,
          .instance = thread->instance,
          .vulkanApiVersion = VK_API_VERSION_1_0,
          .pAllocationCallbacks = &self->vk_host_alloc_cb,
          .pDeviceMemoryCallbacks = &vma_callbacks,
      };
      err = vmaCreateAllocator(&create_info, &self->vma_alloc);
      TB_VK_CHECK_RET(err, "Failed to create vma allocator for render system",
                      false);
    }

    // Initialize all game thread render frame state
    for (uint32_t state_idx = 0; state_idx < MAX_FRAME_STATES; ++state_idx) {
      RenderSystemFrameState *state = &self->frame_states[state_idx];

      // Create tmp host vma pool
      {
        uint32_t host_mem_type_idx = 0xFFFFFFFF;
        // Find the desired memory type index
        for (uint32_t i = 0; i < thread->gpu_mem_props.memoryTypeCount; ++i) {
          VkMemoryType type = thread->gpu_mem_props.memoryTypes[i];
          if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            host_mem_type_idx = i;
            break;
          }
        }
        TB_CHECK_RETURN(host_mem_type_idx != 0xFFFFFFFF,
                        "Failed to find host visible memory", false);

        VmaPoolCreateInfo create_info = {
            .memoryTypeIndex = host_mem_type_idx,
            .flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
            .maxBlockCount = MAX_VMA_TMP_HOST_BLOCK_COUNT,
        };
        err =
            vmaCreatePool(self->vma_alloc, &create_info, &state->tmp_host_pool);
        TB_VK_CHECK_RET(err, "Failed to create vma temp host pool", false);
      }
    }
  }
  return true;
}

void destroy_render_system(RenderSystem *self) {
  VmaAllocator vma_alloc = self->vma_alloc;
  for (uint32_t state_idx = 0; state_idx < MAX_FRAME_STATES; ++state_idx) {
    RenderSystemFrameState *state = &self->frame_states[state_idx];
    for (uint32_t i = 0; i < state->tmp_host_blocks_allocated; ++i) {
      vmaFreeMemory(vma_alloc, state->tmp_host_allocs[i]);
    }
    vmaDestroyPool(vma_alloc, state->tmp_host_pool);
  }
  vmaDestroyAllocator(vma_alloc);
  *self = (RenderSystem){0};
}

void tick_render_system(RenderSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output; // Won't actually have output to the world but will write to
                // the screen we hope
  (void)delta_seconds;

  TracyCZoneN(tick_ctx, "Render System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorRendering);

  // Wait for the render thread to finish the frame with this index
  {
    TracyCZoneN(wait_ctx, "Wait for Render Thread", true);
    TracyCZoneColor(wait_ctx, TracyCategoryColorWait);
    tb_wait_render(self->render_thread, self->frame_idx);
    TracyCZoneEnd(wait_ctx);
  }

  // Reset temp pools
  {
    // Here we reset the *previous* frame's pools because the next frame
    // will probably be edited by the main thread now. The previous frame
    // should have already been rendered by now so we know we can
    // free those resources
    const uint32_t next_frame_idx = (self->frame_idx - 1) % MAX_FRAME_STATES;

    RenderSystemFrameState *prev_state = &self->frame_states[next_frame_idx];

    for (uint32_t i = 0; i < prev_state->tmp_host_blocks_allocated; ++i) {
      vmaFreeMemory(self->vma_alloc, prev_state->tmp_host_allocs[i]);
    }
    SDL_memset(prev_state->tmp_host_allocs, 0,
               sizeof(VmaAllocation) * MAX_VMA_TMP_HOST_BLOCK_COUNT);
    prev_state->tmp_host_blocks_allocated = 0;
  }

  {
    TracyCZoneN(ctx, "Render System Tick", true);
    TracyCZoneColor(ctx, TracyCategoryColorRendering);

    // TODO
    // SDL_Log("Render System: last idx (%d), this idx", self->last_frame_idx,
    //        this_frame_idx);

    TracyCZoneEnd(ctx);
  }

  // Signal the render thread to start rendering this frame
  tb_signal_render(self->render_thread, self->frame_idx);
  self->frame_idx = (self->frame_idx + 1) % MAX_FRAME_STATES;

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(render, RenderSystem, RenderSystemDescriptor)

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc) {
  desc->name = "Render";
  desc->size = sizeof(RenderSystem);
  desc->id = RenderSystemId;
  desc->desc = (InternalDescriptor)render_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 0;
  desc->create = tb_create_render_system;
  desc->destroy = tb_destroy_render_system;
  desc->tick = tb_tick_render_system;
}

bool tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                      uint32_t memory_usage,
                                      uint32_t buffer_usage, VkBuffer *buffer) {
  VkResult err = VK_SUCCESS;

  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];

  VkBufferCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = buffer_usage,
  };
  VmaAllocationCreateInfo alloc_create_info = {
      .pool = state->tmp_host_pool,
      .usage = memory_usage,
  };
  VmaAllocation *alloc =
      &state->tmp_host_allocs[state->tmp_host_blocks_allocated];
  VmaAllocationInfo alloc_info = {0};
  err = vmaCreateBuffer(self->vma_alloc, &create_info, &alloc_create_info,
                        buffer, alloc, &alloc_info);
  TB_VK_CHECK_RET(err, "Failed to allocate temporary buffer", false);

  state->tmp_host_blocks_allocated++;
  return true;
}
