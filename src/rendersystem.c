#include "rendersystem.h"

#include "mimalloc.h"
#include "profiling.h"
#include "renderthread.h"
#include "tbcommon.h"
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
      .tmp_alloc = desc->tmp_alloc,
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
    for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
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

      // Create gpu image pool
      {
        uint32_t gpu_img_mem_type_idx = 0xFFFFFFFF;
        // Find the desired memory type index
        for (uint32_t i = 0; i < thread->gpu_mem_props.memoryTypeCount; ++i) {
          VkMemoryType type = thread->gpu_mem_props.memoryTypes[i];
          if (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            gpu_img_mem_type_idx = i;
            break;
          }
        }
        TB_CHECK_RETURN(gpu_img_mem_type_idx != 0xFFFFFFFF,
                        "Failed to find gpu visible memory", false);

        VmaPoolCreateInfo create_info = {
            .memoryTypeIndex = gpu_img_mem_type_idx,
        };
        err = vmaCreatePool(self->vma_alloc, &create_info,
                            &state->gpu_image_pool);
        TB_VK_CHECK_RET(err, "Failed to create vma host image pool", false);
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

        data = tb_alloc(self->tmp_alloc, data_size);

        SDL_RWread(cache_file, data, data_size, 1);
        SDL_RWclose(cache_file);
      }
      VkPipelineCacheCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
          .initialDataSize = data_size,
          .pInitialData = data,
      };
      err =
          vkCreatePipelineCache(thread->device, &create_info,
                                &self->vk_host_alloc_cb, &self->pipeline_cache);
      TB_VK_CHECK_RET(err, "Failed to create pipeline cache", false);
      SET_VK_NAME(thread->device, self->pipeline_cache,
                  VK_OBJECT_TYPE_PIPELINE_CACHE, "Toybox Pipeline Cache");
    }
  }
  return true;
}

void destroy_render_system(RenderSystem *self) {
  VmaAllocator vma_alloc = self->vma_alloc;

  // Wait for all frame states to finish so that the queue and device are not in
  // use
  for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
    tb_wait_render(self->render_thread, state_idx);
  }
  VkDevice device = self->render_thread->device;
  vkDeviceWaitIdle(device);

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

    vmaUnmapMemory(vma_alloc, state->tmp_host_alloc);
    vmaDestroyBuffer(vma_alloc, state->tmp_host_buffer, state->tmp_host_alloc);
    vmaDestroyPool(vma_alloc, state->tmp_host_pool);

    vmaDestroyPool(vma_alloc, state->gpu_image_pool);
  }

  // Re-signal all frame states to flush the thread
  for (uint32_t state_idx = 0; state_idx < TB_MAX_FRAME_STATES; ++state_idx) {
    tb_signal_render(self->render_thread, state_idx);
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
      const uint32_t next_frame_idx =
          (self->frame_idx + 1) % TB_MAX_FRAME_STATES;
      RenderSystemFrameState *prev_state = &self->frame_states[next_frame_idx];
      prev_state->tmp_host_size = 0;
    }

    RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
    FrameState *thread_state =
        &self->render_thread->frame_states[self->frame_idx];

    // Copy this frame state's temp buffer to the gpu
    {
      BufferCopy up = {
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
      thread_state->buf_copy_queue = state->buf_copy_queue;
      thread_state->buf_img_copy_queue = state->buf_img_copy_queue;
      state->buf_copy_queue.req_count = 0;
      state->buf_img_copy_queue.req_count = 0;
    }

    TracyCZoneEnd(ctx);
  }

  // Signal the render thread to start rendering this frame
  tb_signal_render(self->render_thread, self->frame_idx);
  self->frame_idx = (self->frame_idx + 1) % TB_MAX_FRAME_STATES;

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

VkResult tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                          TbBuffer *buffer) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];

  void *ptr = &state->tmp_host_mapped[state->tmp_host_size];

  // Always 16 byte aligned
  intptr_t padding = 0;
  if ((intptr_t)ptr % 16 != 0) {
    padding = (16 - (intptr_t)ptr % 16);
  }
  ptr = (void *)((intptr_t)ptr + padding);

  TB_CHECK_RETURN((intptr_t)ptr % 16 == 0, "Failed to align allocation",
                  VK_ERROR_OUT_OF_HOST_MEMORY);

  const uint64_t offset = state->tmp_host_size;

  state->tmp_host_size += (size + padding);

  buffer->buffer = state->tmp_host_buffer;
  buffer->offset = offset;
  buffer->ptr = ptr;

  return VK_SUCCESS;
}

VkResult tb_rnd_sys_alloc_gpu_image(RenderSystem *self,
                                    const VkImageCreateInfo *create_info,
                                    const char *name, TbImage *image) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];

  VmaAllocator vma_alloc = self->vma_alloc;
  VmaPool pool = state->gpu_image_pool;

  VmaAllocationCreateInfo alloc_create_info = {
      .memoryTypeBits = VMA_MEMORY_USAGE_GPU_ONLY,
      .pool = pool,
  };
  VkResult err = vmaCreateImage(vma_alloc, create_info, &alloc_create_info,
                                &image->image, &image->alloc, &image->info);
  TB_VK_CHECK_RET(err, "Failed to allocate gpu image", err);

  SET_VK_NAME(self->render_thread->device, image->image, VK_OBJECT_TYPE_IMAGE,
              name);

  return VK_SUCCESS;
}

VkBuffer tb_rnd_get_gpu_tmp_buffer(RenderSystem *self) {
  return self->render_thread->frame_states[self->frame_idx].tmp_gpu_buffer;
}

void tb_rnd_register_pass(RenderSystem *self, VkRenderPass pass,
                          VkFramebuffer *framebuffers,
                          tb_pass_record *record_cb) {
  Allocator std_alloc = self->std_alloc;
  for (uint32_t frame_idx = 0; frame_idx < TB_MAX_FRAME_STATES; ++frame_idx) {
    FrameState *state = &self->render_thread->frame_states[frame_idx];

    const uint32_t new_count = state->pass_count + 1;
    if (new_count > state->pass_max) {
      const uint32_t new_max = new_count * 2;
      state->pass_draw_contexts = tb_realloc_nm_tp(
          std_alloc, state->pass_draw_contexts, new_max, PassDrawCtx);
      state->pass_max = new_max;
    }

    state->pass_draw_contexts[state->pass_count] = (PassDrawCtx){
        .pass = pass,
        .framebuffer = framebuffers[frame_idx],
        .record_cb = record_cb,
    };
    state->pass_count = new_count;
  }
}

void tb_rnd_issue_draw_batch(RenderSystem *self, VkRenderPass pass,
                             uint32_t batch_count, uint64_t batch_size,
                             const void *batches) {
  // TOOD: rethink this... the tmp allocator may not be sufficient here
  Allocator tmp_alloc = self->tmp_alloc;

  FrameState *state = &self->render_thread->frame_states[self->frame_idx];
  for (uint32_t pass_idx = 0; pass_idx < state->pass_count; ++pass_idx) {
    PassDrawCtx *ctx = &state->pass_draw_contexts[pass_idx];
    if (ctx->pass == pass) {
      // Allocate space in the render thread's temp allocator for the next frame
      // for
      // the draw batches
      const uint64_t batch_bytes = batch_size * batch_count;
      void *batch_dst = tb_alloc(tmp_alloc, batch_bytes);

      // Copy draw batches
      SDL_memcpy(batch_dst, batches, batch_bytes);

      ctx->batch_count = batch_count;
      ctx->batch_size = batch_size;
      ctx->batches = batch_dst;
      break;
    }
  }
}

VkResult tb_rnd_create_render_pass(RenderSystem *self,
                                   const VkRenderPassCreateInfo *create_info,
                                   const char *name, VkRenderPass *pass) {
  VkResult err = VK_SUCCESS;

  err = vkCreateRenderPass(self->render_thread->device, create_info,
                           &self->vk_host_alloc_cb, pass);
  TB_VK_CHECK_RET(err, "Failed to create render pass", err);

  SET_VK_NAME(self->render_thread->device, *pass, VK_OBJECT_TYPE_RENDER_PASS,
              name);

  return err;
}

void tb_rnd_upload_buffers(RenderSystem *self, BufferCopy *uploads,
                           uint32_t upload_count) {
  const uint32_t frame_idx = self->frame_idx;

  RenderSystemFrameState *state = &self->frame_states[frame_idx];
  BufferCopyQueue *queue = &state->buf_copy_queue;

  // See if we need to resize queue
  Allocator std_alloc = self->std_alloc;
  const uint64_t new_count = queue->req_count + upload_count;
  if (queue->req_max < new_count) {
    const uint64_t new_max = new_count * 2;
    queue->reqs = tb_realloc_nm_tp(std_alloc, queue->reqs, new_max, BufferCopy);
    queue->req_max = new_max;
  }

  // Append uploads to queue
  SDL_memcpy(&queue->reqs[queue->req_count], uploads,
             sizeof(BufferCopy) * upload_count);

  queue->req_count = new_count;
}

void tb_rnd_upload_buffer_to_image(RenderSystem *self, BufferImageCopy *uploads,
                                   uint32_t upload_count) {
  const uint32_t frame_idx = self->frame_idx;

  RenderSystemFrameState *state = &self->frame_states[frame_idx];
  BufferImageCopyQueue *queue = &state->buf_img_copy_queue;

  // See if we need to resize queue
  Allocator std_alloc = self->std_alloc;
  const uint64_t new_count = queue->req_count + upload_count;
  if (queue->req_max < new_count) {
    const uint64_t new_max = new_count * 2;
    queue->reqs =
        tb_realloc_nm_tp(std_alloc, queue->reqs, new_max, BufferImageCopy);
    queue->req_max = new_max;
  }

  // Append uploads to queue
  SDL_memcpy(&queue->reqs[queue->req_count], uploads,
             sizeof(BufferImageCopy) * upload_count);

  queue->req_count = new_count;
}

void tb_rnd_free_gpu_image(RenderSystem *self, TbImage *image) {
  tb_wait_render(self->render_thread, self->frame_idx);
  vkDeviceWaitIdle(self->render_thread->device);

  vmaDestroyImage(self->vma_alloc, image->image, image->alloc);

  tb_signal_render(self->render_thread, self->frame_idx);
}

void tb_rnd_destroy_render_pass(RenderSystem *self, VkRenderPass pass) {
  vkDestroyRenderPass(self->render_thread->device, pass,
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

void tb_rnd_destroy_pipeline(RenderSystem *self, VkPipeline pipeline) {
  vkDestroyPipeline(self->render_thread->device, pipeline,
                    &self->vk_host_alloc_cb);
}
