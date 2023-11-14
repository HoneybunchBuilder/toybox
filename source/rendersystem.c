#include "rendersystem.h"

#include "mimalloc.h"
#include "profiling.h"
#include "renderthread.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>

bool try_map(VmaAllocator vma, VmaAllocation alloc, void **ptr) {
  VkMemoryPropertyFlags flags = 0;
  vmaGetAllocationMemoryProperties(vma, alloc, &flags);

  // If this buffer is host visible, just get the pointer to write to
  if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    VmaAllocationInfo alloc_info = {0};
    vmaGetAllocationInfo(vma, alloc, &alloc_info);
    // If allocation wasn't mapped, map it
    if (!alloc_info.pMappedData) {
      VkResult err = vmaMapMemory(vma, alloc, ptr);
      TB_VK_CHECK_RET(err, "Failed to map memory", false);
    } else {
      *ptr = alloc_info.pMappedData;
    }
    return true;
  } else {
    return false;
  }
}

void tb_flush_alloc(RenderSystem *self, VmaAllocation alloc) {
  VkMemoryPropertyFlags flags = 0;
  vmaGetAllocationMemoryProperties(self->vma_alloc, alloc, &flags);
  if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) {
    vmaFlushAllocation(self->vma_alloc, alloc, 0, VK_WHOLE_SIZE);
  }
}

RenderSystem create_render_system(Allocator std_alloc, Allocator tmp_alloc,
                                  RenderThread *thread) {
  TB_CHECK(thread, "Invalid RenderThread");

  RenderSystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .render_thread = thread,
  };

  // Initialze some arrays who are primarily owned by the render thread
  // but which are primarily interacted with via the game thread
  // TODO: Think of a better way to do this; the ownership practice here is
  // sloppy
  for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
    FrameState *state = &sys.render_thread->frame_states[state_idx];
    TB_DYN_ARR_RESET(state->pass_contexts, sys.std_alloc, 1);
    TB_DYN_ARR_RESET(state->draw_contexts, sys.std_alloc, 1);
    TB_DYN_ARR_RESET(state->dispatch_contexts, sys.std_alloc, 1);
  }

  // Should be safe to assume that the render thread is initialized by now
  {
    VkResult err = VK_SUCCESS;

    // Create vulkan allocator for main thread
    sys.vk_host_alloc_cb = (VkAllocationCallbacks){
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
          .vkGetBufferMemoryRequirements2KHR =
              vkGetBufferMemoryRequirements2KHR,
          .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR,
          .vkBindBufferMemory2KHR = vkBindBufferMemory2KHR,
          .vkBindImageMemory2KHR = vkBindImageMemory2KHR,
          .vkGetPhysicalDeviceMemoryProperties2KHR =
              vkGetPhysicalDeviceMemoryProperties2KHR,
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
          .pAllocationCallbacks = &sys.vk_host_alloc_cb,
          .pDeviceMemoryCallbacks = &vma_callbacks,
      };
      err = vmaCreateAllocator(&create_info, &sys.vma_alloc);
      TB_VK_CHECK(err, "Failed to create vma allocator for render system");
    }

    // Initialize all game thread render frame state
    for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
      RenderSystemFrameState *state = &sys.frame_states[state_idx];

      TB_DYN_ARR_RESET(state->set_write_queue, sys.std_alloc, 1);
      TB_DYN_ARR_RESET(state->buf_copy_queue, sys.std_alloc, 1);
      TB_DYN_ARR_RESET(state->buf_img_copy_queue, sys.std_alloc, 1);

      // Allocate tmp host buffer
      {
        VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = TB_VMA_TMP_HOST_MB * 1024 * 1024,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        VmaAllocationCreateInfo alloc_create_info = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        err = vmaCreateBuffer(sys.vma_alloc, &create_info, &alloc_create_info,
                              &state->tmp_host_buffer.buffer,
                              &state->tmp_host_buffer.alloc,
                              &state->tmp_host_buffer.info);
        state->tmp_host_buffer.info.size = 0; // we use size as a tracker
        TB_VK_CHECK(err, "Failed to allocate temporary buffer");
        SET_VK_NAME(sys.render_thread->device, state->tmp_host_buffer.buffer,
                    VK_OBJECT_TYPE_BUFFER, "Vulkan Tmp Host Buffer");
      }
    }

    // Load the pipeline cache
    {
      size_t data_size = 0;
      void *data = NULL;

      // If an existing pipeline cache exists, load it
      SDL_RWops *cache_file = SDL_RWFromFile("./pipeline.cache", "rb");
      if (cache_file != NULL) {
        data_size = (size_t)SDL_RWsize(cache_file);

        data = tb_alloc(sys.tmp_alloc, data_size);

        SDL_RWread(cache_file, data, data_size, 1);
        SDL_RWclose(cache_file);
      }
      VkPipelineCacheCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
          .initialDataSize = data_size,
          .pInitialData = data,
      };
      err = vkCreatePipelineCache(thread->device, &create_info,
                                  &sys.vk_host_alloc_cb, &sys.pipeline_cache);
      TB_VK_CHECK(err, "Failed to create pipeline cache");
      SET_VK_NAME(thread->device, sys.pipeline_cache,
                  VK_OBJECT_TYPE_PIPELINE_CACHE, "Toybox Pipeline Cache");
    }
  }

  return sys;
}

void destroy_render_system(RenderSystem *self) {
  // Assume we have already stopped the render thread at this point
  VmaAllocator vma_alloc = self->vma_alloc;

  VkDevice device = self->render_thread->device;

  // Write out pipeline cache
  {
    VkResult err = VK_SUCCESS;

    size_t cache_size = 0;
    err =
        vkGetPipelineCacheData(device, self->pipeline_cache, &cache_size, NULL);
    if (err == VK_SUCCESS) {
      void *cache = tb_alloc(self->tmp_alloc, cache_size);
      err = vkGetPipelineCacheData(device, self->pipeline_cache, &cache_size,
                                   cache);
      if (err == VK_SUCCESS) {
        SDL_RWops *cache_file = SDL_RWFromFile("./pipeline.cache", "wb");
        if (cache_file != NULL) {
          SDL_RWwrite(cache_file, cache, cache_size, 1);
          SDL_RWclose(cache_file);
        }
      }
    }

    vkDestroyPipelineCache(device, self->pipeline_cache,
                           &self->vk_host_alloc_cb);
  }

  for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
    RenderSystemFrameState *state = &self->frame_states[state_idx];

    vmaUnmapMemory(vma_alloc, state->tmp_host_buffer.alloc);
    vmaDestroyBuffer(vma_alloc, state->tmp_host_buffer.buffer,
                     state->tmp_host_buffer.alloc);
    TB_DYN_ARR_DESTROY(state->set_write_queue);
    TB_DYN_ARR_DESTROY(state->buf_copy_queue);
    TB_DYN_ARR_DESTROY(state->buf_img_copy_queue);
  }

  // Clean up main thread owned memory that the render thread held the primary
  // reference on
  // TODO: Think of a better way to do this; the ownership practice here is
  // sloppy
  for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
    FrameState *state = &self->render_thread->frame_states[state_idx];
    TB_DYN_ARR_DESTROY(state->pass_contexts);
    TB_DYN_ARR_DESTROY(state->draw_contexts);
    TB_DYN_ARR_DESTROY(state->dispatch_contexts);
  }

  // Re-signal all frame states to flush the thread
  for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
    tb_signal_render(self->render_thread, state_idx);
  }

  vmaDestroyAllocator(vma_alloc);
  *self = (RenderSystem){0};
}

void render_frame_begin(ecs_iter_t *it) {
  RenderSystem *sys = ecs_field(it, RenderSystem, 1);
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Frame Start Tick");

  TracyCZoneNC(wait_ctx, "Wait for Render Thread", TracyCategoryColorWait,
               true);
  TracyCZoneValue(wait_ctx, sys->frame_idx);
  tb_wait_render(sys->render_thread, sys->frame_idx);
  TracyCZoneEnd(wait_ctx);

  // Also
  // Manually zero out the previous frame's draw batches here
  // It's cleaner to do it here than dedicate a whole system to this
  // operation on tick
  FrameState *state = &sys->render_thread->frame_states[sys->frame_idx];

  TB_DYN_ARR_FOREACH(state->draw_contexts, i) {
    TB_DYN_ARR_AT(state->draw_contexts, i).batch_count = 0;
  }
  TB_DYN_ARR_FOREACH(state->dispatch_contexts, i) {
    TB_DYN_ARR_AT(state->dispatch_contexts, i).batch_count = 0;
  }
}

void render_frame_end(ecs_iter_t *it) {
  RenderSystem *sys = ecs_field(it, RenderSystem, 1);
  TracyCZoneN(tick_ctx, "Render System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorRendering);

  {
    TracyCZoneN(ctx, "Render System Tick", true);
    TracyCZoneColor(ctx, TracyCategoryColorRendering);

    RenderSystemFrameState *state = &sys->frame_states[sys->frame_idx];
    FrameState *thread_state =
        &sys->render_thread->frame_states[sys->frame_idx];

    // Copy this frame state's temp buffer to the gpu
    if (state->tmp_host_buffer.info.size > 0) {
      // Flush the tmp buffer
      tb_flush_alloc(sys, thread_state->tmp_gpu_alloc);

      // Only copy if we know that we weren't able to just write directly
      // to this buffer
      VkMemoryPropertyFlags flags = 0;
      vmaGetAllocationMemoryProperties(sys->vma_alloc,
                                       thread_state->tmp_gpu_alloc, &flags);
      if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        BufferCopy up = {
            .dst = thread_state->tmp_gpu_buffer,
            .src = state->tmp_host_buffer.buffer,
            .region =
                {
                    .dstOffset = 0,
                    .srcOffset = 0,
                    .size = state->tmp_host_buffer.info.size,
                },
        };
        tb_rnd_upload_buffers(sys, &up, 1);
      }
    }

    // Send and Reset buffer upload pool
    {
      // Assign to the thread
      thread_state->set_write_queue = state->set_write_queue;
      thread_state->buf_copy_queue = state->buf_copy_queue;
      thread_state->buf_img_copy_queue = state->buf_img_copy_queue;
      TB_DYN_ARR_CLEAR(state->set_write_queue);
      TB_DYN_ARR_CLEAR(state->buf_copy_queue);
      TB_DYN_ARR_CLEAR(state->buf_img_copy_queue);
    }

    // Reset temp pool, the contents will still be intact for the render thread
    // but it will be reset for the next time this frame is processed
    {
      RenderSystemFrameState *state = &sys->frame_states[sys->frame_idx];
      state->tmp_host_buffer.info.size = 0;
    }

    TracyCZoneEnd(ctx);
  }

  // Signal the render thread to start rendering this frame
  tb_signal_render(sys->render_thread, sys->frame_idx);
  sys->frame_idx = (sys->frame_idx + 1) % TB_MAX_FRAME_STATES;

  TracyCZoneEnd(tick_ctx);
}

void tb_register_render_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc, RenderThread *render_thread) {
  ECS_COMPONENT(ecs, RenderSystem);
  RenderSystem sys = create_render_system(std_alloc, tmp_alloc, render_thread);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(RenderSystem), RenderSystem, &sys);

  ECS_SYSTEM(ecs, render_frame_begin, EcsPreUpdate, RenderSystem(RenderSystem));
  ECS_SYSTEM(ecs, render_frame_end, EcsPostFrame, RenderSystem(RenderSystem));
}

void tb_unregister_render_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, RenderSystem);
  RenderSystem *sys = ecs_singleton_get_mut(ecs, RenderSystem);
  destroy_render_system(sys);
  ecs_singleton_remove(ecs, RenderSystem);
}

VkResult alloc_tmp_buffer(RenderSystem *self, uint64_t size, uint32_t alignment,
                          TbHostBuffer *buffer) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
  FrameState *thread_state =
      &self->render_thread->frame_states[self->frame_idx];

  void *ptr = NULL;

  // Try to map the tmp buffer that is device local
  // If we can't do that we rely on the host buffer
  if (!try_map(self->vma_alloc, thread_state->tmp_gpu_alloc, &ptr)) {
    ptr = state->tmp_host_buffer.info.pMappedData;
  }

  ptr = &((uint8_t *)ptr)[state->tmp_host_buffer.info.size];

  intptr_t padding = 0;
  if (alignment > 0 && (intptr_t)ptr % alignment != 0) {
    padding = (alignment - (intptr_t)ptr % alignment);
  }
  ptr = (void *)((intptr_t)ptr + padding);

  TB_CHECK_RETURN(alignment == 0 || (intptr_t)ptr % alignment == 0,
                  "Failed to align allocation", VK_ERROR_OUT_OF_HOST_MEMORY);

  const uint64_t offset = state->tmp_host_buffer.info.size + padding;

  state->tmp_host_buffer.info.size += (size + padding);

  buffer->buffer = state->tmp_host_buffer.buffer;
  buffer->offset = offset;
  buffer->info.pMappedData = ptr;

  return VK_SUCCESS;
}

VkResult alloc_host_buffer(RenderSystem *self,
                           const VkBufferCreateInfo *create_info,
                           const char *name, TbHostBuffer *buffer) {
  VmaAllocator vma_alloc = self->vma_alloc;

  VmaAllocationCreateInfo alloc_create_info = {
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
  };
  VkResult err =
      vmaCreateBuffer(vma_alloc, create_info, &alloc_create_info,
                      &buffer->buffer, &buffer->alloc, &buffer->info);
  TB_VK_CHECK_RET(err, "Failed to allocate host buffer", err);
  SET_VK_NAME(self->render_thread->device, buffer->buffer,
              VK_OBJECT_TYPE_BUFFER, name);

  buffer->offset = 0;

  return VK_SUCCESS;
}

VkResult tb_rnd_sys_alloc_gpu_buffer(RenderSystem *self,
                                     const VkBufferCreateInfo *create_info,
                                     const char *name, TbBuffer *buffer) {
  VmaAllocator vma_alloc = self->vma_alloc;

  VmaAllocationCreateInfo alloc_create_info = {
      .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
  };

  VkResult err =
      vmaCreateBuffer(vma_alloc, create_info, &alloc_create_info,
                      &buffer->buffer, &buffer->alloc, &buffer->info);
  TB_VK_CHECK_RET(err, "Failed to allocate gpu buffer", err);

  SET_VK_NAME(self->render_thread->device, buffer->buffer,
              VK_OBJECT_TYPE_BUFFER, name);

  return err;
}

VkResult tb_rnd_sys_alloc_gpu_image(RenderSystem *self,
                                    const VkImageCreateInfo *create_info,
                                    VmaAllocationCreateFlags vma_flags,
                                    const char *name, TbImage *image) {
  VmaAllocator vma_alloc = self->vma_alloc;
  VmaAllocationCreateInfo alloc_create_info = {
      .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .flags = vma_flags,
  };
  VkResult err = vmaCreateImage(vma_alloc, create_info, &alloc_create_info,
                                &image->image, &image->alloc, &image->info);
  TB_VK_CHECK_RET(err, "Failed to allocate gpu image", err);

  SET_VK_NAME(self->render_thread->device, image->image, VK_OBJECT_TYPE_IMAGE,
              name);
  return err;
}

VkResult tb_rnd_sys_tmp_buffer_copy(RenderSystem *self, uint64_t size,
                                    uint32_t alignment, const void *data,
                                    uint64_t *offset) {
  void *ptr = NULL;
  VkResult err =
      tb_rnd_sys_tmp_buffer_get_ptr(self, size, alignment, offset, &ptr);
  TB_VK_CHECK_RET(err, "Failed to copy data to tmp buffer", err);
  SDL_memcpy(ptr, data, size);
  return err;
}

VkResult tb_rnd_sys_tmp_buffer_get_ptr(RenderSystem *self, uint64_t size,
                                       uint32_t alignment, uint64_t *offset,
                                       void **ptr) {
  VkResult err = VK_SUCCESS;

  // Make an allocation on the tmp buffer
  TbHostBuffer host_buffer = {0};
  err = alloc_tmp_buffer(self, size, alignment, &host_buffer);
  *offset = host_buffer.offset;
  *ptr = host_buffer.info.pMappedData;

  return err;
}

VkResult tb_rnd_sys_create_gpu_buffer(RenderSystem *self,
                                      const VkBufferCreateInfo *create_info,
                                      const char *name, TbBuffer *buffer,
                                      TbHostBuffer *host, void **ptr) {
  // Create the GPU side buffer
  VkResult err = tb_rnd_sys_alloc_gpu_buffer(self, create_info, name, buffer);

  // If this buffer is host visible, just get the pointer to write to
  if (try_map(self->vma_alloc, buffer->alloc, ptr)) {
    return err;
  }

  // Otherwise we have to create a host buffer and schedule an upload

  // It's the caller's responsibility to handle the host buffer too
  // even if it's not going to be used on non-uma systems
  err = alloc_host_buffer(self, create_info, name, host);
  TB_VK_CHECK_RET(err, "Failed to alloc host buffer", err);

  BufferCopy upload = {
      .src = host->buffer,
      .dst = buffer->buffer,
      .region =
          {
              .size = create_info->size,
          },
  };
  tb_rnd_upload_buffers(self, &upload, 1);

  *ptr = host->info.pMappedData;
  return err;
}

VkResult tb_rnd_sys_create_gpu_buffer_tmp(RenderSystem *self,
                                          const VkBufferCreateInfo *create_info,
                                          const char *name, TbBuffer *buffer,
                                          uint32_t alignment, void **ptr) {
  // Create the GPU side buffer
  VkResult err = tb_rnd_sys_alloc_gpu_buffer(self, create_info, name, buffer);

  // If this buffer is host visible, just get the pointer to write to
  if (try_map(self->vma_alloc, buffer->alloc, ptr)) {
    return err;
  }

  // Otherwise we have to create write to the tmp buffer and schedule an
  // upload
  TbHostBuffer host = {0};
  err = alloc_tmp_buffer(self, create_info->size, alignment, &host);
  TB_VK_CHECK_RET(err, "Failed to alloc host buffer", err);

  BufferCopy upload = {
      .src = tb_rnd_get_gpu_tmp_buffer(self),
      .dst = buffer->buffer,
      .region =
          {
              .srcOffset = host.offset,
              .size = create_info->size,
          },
  };
  tb_rnd_upload_buffers(self, &upload, 1);

  *ptr = host.info.pMappedData;
  return err;
}

VkResult tb_rnd_sys_create_gpu_buffer2(RenderSystem *self,
                                       const VkBufferCreateInfo *create_info,
                                       const void *data, const char *name,
                                       TbBuffer *buffer, TbHostBuffer *host) {
  void *ptr = NULL;
  VkResult err =
      tb_rnd_sys_create_gpu_buffer(self, create_info, name, buffer, host, &ptr);
  TB_VK_CHECK_RET(err, "Failed to create GPU buffer", err);
  SDL_memcpy(ptr, data, create_info->size);
  tb_flush_alloc(self, buffer->alloc);
  return err;
}

VkResult tb_rnd_sys_create_gpu_buffer2_tmp(
    RenderSystem *self, const VkBufferCreateInfo *create_info, const void *data,
    const char *name, TbBuffer *buffer, uint32_t alignment) {
  void *ptr = NULL;
  VkResult err = tb_rnd_sys_create_gpu_buffer_tmp(self, create_info, name,
                                                  buffer, alignment, &ptr);
  TB_VK_CHECK_RET(err, "Failed to create GPU buffer", err);
  SDL_memcpy(ptr, data, create_info->size);
  tb_flush_alloc(self, buffer->alloc);
  return err;
}

// This needs to be seriously re-thought from the perspective of
// texture streaming
// For now scheduling uploads will be the responsibility of the caller
VkResult tb_rnd_sys_create_gpu_image(RenderSystem *self, const void *data,
                                     uint64_t data_size,
                                     const VkImageCreateInfo *create_info,
                                     const char *name, TbImage *image,
                                     TbHostBuffer *host) {
  VkResult err = tb_rnd_sys_alloc_gpu_image(self, create_info, 0, name, image);
  TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture", err);

  void *ptr = NULL;

  // See if we can just write to the image
  if (!try_map(self->vma_alloc, image->alloc, &ptr)) {
    // Allocate memory on the host and copy data to it
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = data_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    err = alloc_host_buffer(self, &buffer_info, name, host);
    TB_VK_CHECK_RET(err, "Failed to allocate host buffer for texture", err);

    ptr = host->info.pMappedData;
  }

  // Copy data to the buffer
  SDL_memcpy(ptr, data, data_size);
  tb_flush_alloc(self, image->alloc);

  return err;
}

// Copy the given data to the temp buffer and move it into a dedicated GPU image
VkResult tb_rnd_sys_create_gpu_image_tmp(RenderSystem *self, const void *data,
                                         uint64_t data_size, uint32_t alignment,
                                         const VkImageCreateInfo *create_info,
                                         const char *name, TbImage *image) {
  VkResult err = tb_rnd_sys_alloc_gpu_image(self, create_info, 0, name, image);
  TB_VK_CHECK_RET(err, "Failed to allocate gpu image for texture", err);

  void *ptr = NULL;

  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceSize offset = 0;

  if (!try_map(self->vma_alloc, image->alloc, &ptr)) {
    // Allocate space for the buffer on the temp buffer
    TbHostBuffer host = {0};
    err = alloc_tmp_buffer(self, data_size, alignment, &host);
    TB_VK_CHECK_RET(err, "Failed to alloc space on the temp buffer", err);
    ptr = host.info.pMappedData;

    // We know that the data on the tmp buffer will be automatically uploaded
    // so instead of issuing another upload, we copy from the uploaded page
    // to the final memory location of the image
    buffer = tb_rnd_get_gpu_tmp_buffer(self);
    offset = host.offset;
  }

  // We do this copy even on UMA platforms because we need the transition
  // If the buffer is null the copy will be skipped but the transition
  // will still occur
  BufferImageCopy copy = {
      .src = buffer,
      .dst = image->image,
      .region =
          {
              .bufferOffset = offset,
              .imageSubresource =
                  {
                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                      .layerCount = 1,
                  },
              .imageExtent =
                  {
                      .width = create_info->extent.width,
                      .height = create_info->extent.height,
                      .depth = 1,
                  },
          },
      .range =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
              .levelCount = 1,
          },
  };
  tb_rnd_upload_buffer_to_image(self, &copy, 1);

  // Copy data to the buffer
  SDL_memcpy(ptr, data, data_size);
  tb_flush_alloc(self, image->alloc);

  return err;
}

VkBuffer tb_rnd_get_gpu_tmp_buffer(RenderSystem *self) {
  return self->render_thread->frame_states[self->frame_idx].tmp_gpu_buffer;
}

VkResult tb_rnd_sys_update_gpu_buffer(RenderSystem *self,
                                      const TbBuffer *buffer,
                                      const TbHostBuffer *host, void **ptr) {
  VkResult err = VK_SUCCESS;
  if (buffer->info.size == 0) {
    return err;
  }

  if (!try_map(self->vma_alloc, buffer->alloc, ptr)) {
    // Schedule another upload
    BufferCopy upload = {
        .src = host->buffer,
        .dst = buffer->buffer,
        .region =
            {
                .srcOffset = host->offset,
                .size = buffer->info.size,
            },
    };
    tb_rnd_upload_buffers(self, &upload, 1);

    *ptr = host->info.pMappedData;
  }
  return err;
}

VkResult tb_rnd_create_sampler(RenderSystem *self,
                               const VkSamplerCreateInfo *create_info,
                               const char *name, VkSampler *sampler) {
  VkResult err = vkCreateSampler(self->render_thread->device, create_info,
                                 &self->vk_host_alloc_cb, sampler);
  TB_VK_CHECK_RET(err, "Failed to create sampler", err);
  SET_VK_NAME(self->render_thread->device, *sampler, VK_OBJECT_TYPE_SAMPLER,
              name);
  return err;
}

VkResult tb_rnd_create_image_view(RenderSystem *self,
                                  const VkImageViewCreateInfo *create_info,
                                  const char *name, VkImageView *view) {
  VkResult err = vkCreateImageView(self->render_thread->device, create_info,
                                   &self->vk_host_alloc_cb, view);
  TB_VK_CHECK_RET(err, "Failed to create image view", err);
  SET_VK_NAME(self->render_thread->device, *view, VK_OBJECT_TYPE_IMAGE_VIEW,
              name);
  return err;
}

VkResult
tb_rnd_create_set_layout(RenderSystem *self,
                         const VkDescriptorSetLayoutCreateInfo *create_info,
                         const char *name, VkDescriptorSetLayout *set_layout) {
  VkResult err =
      vkCreateDescriptorSetLayout(self->render_thread->device, create_info,
                                  &self->vk_host_alloc_cb, set_layout);
  TB_VK_CHECK_RET(err, "Failed to create descriptor set layout", err);
  SET_VK_NAME(self->render_thread->device, *set_layout,
              VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);
  return err;
}

VkResult
tb_rnd_create_pipeline_layout(RenderSystem *self,
                              const VkPipelineLayoutCreateInfo *create_info,
                              const char *name, VkPipelineLayout *pipe_layout) {
  VkResult err =
      vkCreatePipelineLayout(self->render_thread->device, create_info,
                             &self->vk_host_alloc_cb, pipe_layout);
  TB_VK_CHECK_RET(err, "Failed to create pipeline layout", err);
  SET_VK_NAME(self->render_thread->device, *pipe_layout,
              VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);
  return err;
}

VkResult tb_rnd_create_shader(RenderSystem *self,
                              const VkShaderModuleCreateInfo *create_info,
                              const char *name, VkShaderModule *shader) {
  VkResult err = vkCreateShaderModule(self->render_thread->device, create_info,
                                      &self->vk_host_alloc_cb, shader);
  TB_VK_CHECK_RET(err, "Failed to create shader module", err);
  SET_VK_NAME(self->render_thread->device, *shader,
              VK_OBJECT_TYPE_SHADER_MODULE, name);
  return err;
}

VkResult
tb_rnd_create_descriptor_pool(RenderSystem *self,
                              const VkDescriptorPoolCreateInfo *create_info,
                              const char *name, VkDescriptorPool *pool) {
  VkResult err = vkCreateDescriptorPool(
      self->render_thread->device, create_info, &self->vk_host_alloc_cb, pool);
  TB_VK_CHECK_RET(err, "Failed to create descriptor pool", err);
  SET_VK_NAME(self->render_thread->device, *pool,
              VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);
  return err;
}

VkResult
tb_rnd_create_compute_pipelines(RenderSystem *self, uint32_t create_info_count,
                                const VkComputePipelineCreateInfo *create_info,
                                const char *name, VkPipeline *pipelines) {
  VkResult err = vkCreateComputePipelines(
      self->render_thread->device, self->pipeline_cache, create_info_count,
      create_info, &self->vk_host_alloc_cb, pipelines);
  TB_VK_CHECK_RET(err, "Failed to create compute pipeline", err);
  for (uint32_t i = 0; i < create_info_count; ++i) {
    SET_VK_NAME(self->render_thread->device, pipelines[i],
                VK_OBJECT_TYPE_PIPELINE, name);
  }
  return err;
}

VkResult tb_rnd_create_graphics_pipelines(
    RenderSystem *self, uint32_t create_info_count,
    const VkGraphicsPipelineCreateInfo *create_info, const char *name,
    VkPipeline *pipelines) {
  VkResult err = vkCreateGraphicsPipelines(
      self->render_thread->device, self->pipeline_cache, create_info_count,
      create_info, &self->vk_host_alloc_cb, pipelines);
  TB_VK_CHECK_RET(err, "Failed to create graphics pipeline", err);
  for (uint32_t i = 0; i < create_info_count; ++i) {
    SET_VK_NAME(self->render_thread->device, pipelines[i],
                VK_OBJECT_TYPE_PIPELINE, name);
  }
  return err;
}

void tb_rnd_upload_buffers(RenderSystem *self, BufferCopy *uploads,
                           uint32_t upload_count) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
  uint32_t head = TB_DYN_ARR_SIZE(state->buf_copy_queue);
  TB_DYN_ARR_RESIZE(state->buf_copy_queue, head + upload_count);
  // Append uploads to queue
  for (uint32_t i = 0; i < upload_count; ++i) {
    TB_DYN_ARR_AT(state->buf_copy_queue, head + i) = uploads[i];
  }
}

void tb_rnd_upload_buffer_to_image(RenderSystem *self, BufferImageCopy *uploads,
                                   uint32_t upload_count) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
  uint32_t head = TB_DYN_ARR_SIZE(state->buf_img_copy_queue);
  TB_DYN_ARR_RESIZE(state->buf_img_copy_queue, head + upload_count);
  // Append uploads to queue
  for (uint32_t i = 0; i < upload_count; ++i) {
    TB_DYN_ARR_AT(state->buf_img_copy_queue, head + i) = uploads[i];
  }
}

void tb_rnd_free_gpu_buffer(RenderSystem *self, TbBuffer *buffer) {
  vmaDestroyBuffer(self->vma_alloc, buffer->buffer, buffer->alloc);
}

void tb_rnd_free_gpu_image(RenderSystem *self, TbImage *image) {
  vmaDestroyImage(self->vma_alloc, image->image, image->alloc);
}

void tb_rnd_destroy_image_view(RenderSystem *self, VkImageView view) {
  vkDestroyImageView(self->render_thread->device, view,
                     &self->vk_host_alloc_cb);
}

void tb_rnd_destroy_sampler(RenderSystem *self, VkSampler sampler) {
  vkDestroySampler(self->render_thread->device, sampler,
                   &self->vk_host_alloc_cb);
}
void tb_rnd_destroy_set_layout(RenderSystem *self,
                               VkDescriptorSetLayout set_layout) {
  vkDestroyDescriptorSetLayout(self->render_thread->device, set_layout,
                               &self->vk_host_alloc_cb);
}
void tb_rnd_destroy_pipe_layout(RenderSystem *self,
                                VkPipelineLayout pipe_layout) {
  vkDestroyPipelineLayout(self->render_thread->device, pipe_layout,
                          &self->vk_host_alloc_cb);
}

void tb_rnd_destroy_shader(RenderSystem *self, VkShaderModule shader) {
  vkDestroyShaderModule(self->render_thread->device, shader,
                        &self->vk_host_alloc_cb);
}

void tb_rnd_destroy_pipeline(RenderSystem *self, VkPipeline pipeline) {
  vkDestroyPipeline(self->render_thread->device, pipeline,
                    &self->vk_host_alloc_cb);
}

void tb_rnd_destroy_descriptor_pool(RenderSystem *self, VkDescriptorPool pool) {
  vkDestroyDescriptorPool(self->render_thread->device, pool,
                          &self->vk_host_alloc_cb);
}

void tb_rnd_update_descriptors(RenderSystem *self, uint32_t write_count,
                               const VkWriteDescriptorSet *writes) {
  // Queue these writes to be processed by the render thread
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
  uint32_t head = TB_DYN_ARR_SIZE(state->set_write_queue);
  TB_DYN_ARR_RESIZE(state->set_write_queue, head + write_count);

  // We know the frame state isn't in use so it's safe to grab its temp
  // allocator and make sure that descriptor info is properly copied
  Allocator rt_state_tmp_alloc =
      self->render_thread->frame_states[self->frame_idx].tmp_alloc.alloc;

  // Append uploads to queue
  for (uint32_t i = 0; i < write_count; ++i) {
    VkWriteDescriptorSet *write =
        &TB_DYN_ARR_AT(state->set_write_queue, head + i);
    *write = writes[i];

    if (write->pBufferInfo != NULL) {
      VkDescriptorBufferInfo *info =
          tb_alloc_tp(rt_state_tmp_alloc, VkDescriptorBufferInfo);
      *info = *write->pBufferInfo;
      write->pBufferInfo = info;
    }

    if (write->pImageInfo != NULL) {
      VkDescriptorImageInfo *info =
          tb_alloc_tp(rt_state_tmp_alloc, VkDescriptorImageInfo);
      *info = *write->pImageInfo;
      write->pImageInfo = info;
    }
  }
}

VkResult
tb_rnd_frame_desc_pool_tick(RenderSystem *self,
                            const VkDescriptorPoolCreateInfo *pool_info,
                            const VkDescriptorSetLayout *layouts,
                            FrameDescriptorPool *pools, uint32_t set_count) {
  VkResult err = VK_SUCCESS;
  FrameDescriptorPool *pool = &pools[self->frame_idx];

  // Resize the pool
  if (pool->set_count < set_count) {
    if (pool->set_pool) {
      tb_rnd_destroy_descriptor_pool(self, pool->set_pool);
    }

    err =
        tb_rnd_create_descriptor_pool(self, pool_info, "Pool", &pool->set_pool);
    TB_VK_CHECK(err, "Failed to create pool");
    pool->set_count = set_count;
    pool->sets = tb_realloc_nm_tp(self->std_alloc, pool->sets, pool->set_count,
                                  VkDescriptorSet);
  } else {
    vkResetDescriptorPool(self->render_thread->device, pool->set_pool, 0);
    pool->set_count = set_count;
  }

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorSetCount = pool->set_count,
      .descriptorPool = pool->set_pool,
      .pSetLayouts = layouts,
  };
  err = vkAllocateDescriptorSets(self->render_thread->device, &alloc_info,
                                 pool->sets);
  TB_VK_CHECK(err, "Failed to re-allocate descriptor sets");

  return err;
}

VkDescriptorSet tb_rnd_frame_desc_pool_get_set(RenderSystem *self,
                                               FrameDescriptorPool *pools,
                                               uint32_t set_idx) {
  return pools[self->frame_idx].sets[set_idx];
}
