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
      .std_alloc = desc->std_alloc,
      .render_thread = thread,
  };

  // Should be safe to assume that the render thread is initialized by now
  {
    VkResult err = VK_SUCCESS;

    // Create vulkan allocator for main thread
    self->vk_host_alloc_cb = (VkAllocationCallbacks){
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

      const uint64_t size_bytes = TB_VMA_TMP_HOST_MB * 1024 * 1024;

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
            .maxBlockCount = 1,
            .blockSize = size_bytes,
        };
        err =
            vmaCreatePool(self->vma_alloc, &create_info, &state->tmp_host_pool);
        TB_VK_CHECK_RET(err, "Failed to create vma temp host pool", false);
      }

      // Allocate tmp host buffer
      {
        VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size_bytes,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        VmaAllocationCreateInfo alloc_create_info = {
            .pool = state->tmp_host_pool,
            .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        };
        VmaAllocationInfo alloc_info = {0};
        err = vmaCreateBuffer(self->vma_alloc, &create_info, &alloc_create_info,
                              &state->tmp_host_buffer, &state->tmp_host_alloc,
                              &alloc_info);
        TB_VK_CHECK_RET(err, "Failed to allocate temporary buffer", false);
        SET_VK_NAME(self->render_thread->device, state->tmp_host_buffer,
                    VK_OBJECT_TYPE_BUFFER, "Vulkan Tmp Host Buffer");

        err = vmaMapMemory(self->vma_alloc, state->tmp_host_alloc,
                           (void **)&state->tmp_host_mapped);
        TB_VK_CHECK_RET(err, "Failed to map temporary buffer", false);
      }
    }
  }
  return true;
}

void destroy_render_system(RenderSystem *self) {
  VmaAllocator vma_alloc = self->vma_alloc;
  for (uint32_t state_idx = 0; state_idx < MAX_FRAME_STATES; ++state_idx) {
    RenderSystemFrameState *state = &self->frame_states[state_idx];
    vmaUnmapMemory(vma_alloc, state->tmp_host_alloc);
    vmaDestroyBuffer(vma_alloc, state->tmp_host_buffer, state->tmp_host_alloc);
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

  {
    TracyCZoneN(ctx, "Render System Tick", true);
    TracyCZoneColor(ctx, TracyCategoryColorRendering);

    // Reset temp pools
    {
      // Reset the *next* frame's pool. Assume that it has already been
      // consumed by the render thread and that it's now free to nuke in
      // prep for the next frame
      const uint32_t next_frame_idx = (self->frame_idx + 1) % MAX_FRAME_STATES;
      RenderSystemFrameState *prev_state = &self->frame_states[next_frame_idx];
      prev_state->tmp_host_size = 0;
    }

    RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
    FrameState *thread_state =
        &self->render_thread->frame_states[self->frame_idx];

    // Copy this frame state's temp buffer to the gpu
    {
      BufferUpload up = {
          .dst = thread_state->tmp_gpu_buffer,
          .src = state->tmp_host_buffer,
          .region =
              {
                  .dstOffset = 0,
                  .srcOffset = 0,
                  .size = state->tmp_host_size,
              },
      };
      tb_rnd_upload_buffers(self, &up, 1);
    }

    // Send and Reset buffer upload pool
    {
      // Assign to the thread
      thread_state->buffer_up_queue = state->buffer_up_queue;
      state->buffer_up_queue.req_count = 0;
    }

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
                                      VkBuffer *buffer, uint64_t *offset,
                                      void **ptr) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];

  void *tmp_ptr = &state->tmp_host_mapped[state->tmp_host_size];

  // Always 16 byte aligned
  intptr_t padding = (16 - (intptr_t)tmp_ptr % 16);
  tmp_ptr = (void *)((intptr_t)tmp_ptr + padding);

  TB_CHECK_RETURN((intptr_t)ptr % 16 == 0, "Failed to align allocation", false);

  uint64_t off = state->tmp_host_size;

  state->tmp_host_size += (size + padding);

  *buffer = state->tmp_host_buffer;
  *offset = off;
  *ptr = tmp_ptr;

  return true;
}

void tb_rnd_upload_buffers(RenderSystem *self, BufferUpload *uploads,
                           uint32_t upload_count) {
  const uint32_t frame_idx = self->frame_idx;

  RenderSystemFrameState *state = &self->frame_states[frame_idx];
  BufferUploadQueue *queue = &state->buffer_up_queue;

  // See if we need to resize queue
  Allocator std_alloc = self->std_alloc;
  const uint64_t new_count = queue->req_count + upload_count;
  if (queue->req_max < new_count) {
    const uint64_t new_max = new_count * 2;
    queue->reqs =
        tb_realloc_nm_tp(std_alloc, queue->reqs, new_max, BufferUpload);
    queue->req_max = new_max;
  }

  // Append uploads to queue
  SDL_memcpy(&queue->reqs[queue->req_count], uploads,
             sizeof(BufferUpload) * upload_count);

  queue->req_count = new_count;
}
