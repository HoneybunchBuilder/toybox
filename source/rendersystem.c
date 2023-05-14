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
                              &state->tmp_host_buffer.buffer,
                              &state->tmp_host_buffer.alloc, &alloc_info);
        TB_VK_CHECK_RET(err, "Failed to allocate temporary buffer", false);
        SET_VK_NAME(self->render_thread->device, state->tmp_host_buffer.buffer,
                    VK_OBJECT_TYPE_BUFFER, "Vulkan Tmp Host Buffer");

        err = vmaMapMemory(self->vma_alloc, state->tmp_host_buffer.alloc,
                           (void **)&state->tmp_host_buffer.ptr);
        TB_VK_CHECK_RET(err, "Failed to map temporary buffer", false);
      }
    }

    // Create host pools
    {
      uint32_t host_mem_type_idx = SDL_MAX_UINT32;
      // Find the desired memory type index
      for (uint32_t i = 0; i < thread->gpu_mem_props.memoryTypeCount; ++i) {
        VkMemoryType type = thread->gpu_mem_props.memoryTypes[i];
        if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
          host_mem_type_idx = i;
          break;
        }
      }
      TB_CHECK_RETURN(host_mem_type_idx != SDL_MAX_UINT32,
                      "Failed to find host visible memory", false);

      VmaPoolCreateInfo create_info = {
          .memoryTypeIndex = host_mem_type_idx,
      };
      err =
          vmaCreatePool(self->vma_alloc, &create_info, &self->host_buffer_pool);
      TB_VK_CHECK_RET(err, "Failed to create vma host buffer pool", false);
    }

    // Create gpu pools
    {
      uint32_t gpu_mem_type_idx = SDL_MAX_UINT32;
      // Find the desired memory type index
      for (uint32_t i = 0; i < thread->gpu_mem_props.memoryTypeCount; ++i) {
        VkMemoryType type = thread->gpu_mem_props.memoryTypes[i];
        if (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
          gpu_mem_type_idx = i;
          break;
        }
      }
      TB_CHECK_RETURN(gpu_mem_type_idx != SDL_MAX_UINT32,
                      "Failed to find gpu visible memory", false);

      VmaPoolCreateInfo create_info = {
          .memoryTypeIndex = gpu_mem_type_idx,
      };
      err = vmaCreatePool(self->vma_alloc, &create_info, &self->gpu_image_pool);
      TB_VK_CHECK_RET(err, "Failed to create vma gpu image pool", false);

      err =
          vmaCreatePool(self->vma_alloc, &create_info, &self->gpu_buffer_pool);
      TB_VK_CHECK_RET(err, "Failed to create vma gpu buffer pool", false);
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
    vmaDestroyPool(vma_alloc, state->tmp_host_pool);
  }

  vmaDestroyPool(vma_alloc, self->host_buffer_pool);
  vmaDestroyPool(vma_alloc, self->gpu_image_pool);
  vmaDestroyPool(vma_alloc, self->gpu_buffer_pool);

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

  {
    TracyCZoneN(ctx, "Render System Tick", true);
    TracyCZoneColor(ctx, TracyCategoryColorRendering);

    RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
    FrameState *thread_state =
        &self->render_thread->frame_states[self->frame_idx];

    // Copy this frame state's temp buffer to the gpu
    {
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

    // Reset temp pool, the contents will still be intact for the render thread
    // but it will be reset for the next time this frame is processed
    {
      RenderSystemFrameState *state = &self->frame_states[self->frame_idx];
      state->tmp_host_buffer.info.size = 0;
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
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 0;
  desc->create = tb_create_render_system;
  desc->destroy = tb_destroy_render_system;
  desc->tick = tb_tick_render_system;
}

VkResult tb_rnd_sys_alloc_tmp_host_buffer(RenderSystem *self, uint64_t size,
                                          uint32_t alignment,
                                          TbHostBuffer *buffer) {
  RenderSystemFrameState *state = &self->frame_states[self->frame_idx];

  void *ptr = &(
      (uint8_t *)state->tmp_host_buffer.ptr)[state->tmp_host_buffer.info.size];

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
  buffer->ptr = ptr;

  return VK_SUCCESS;
}

VkResult tb_rnd_sys_alloc_host_buffer(RenderSystem *self,
                                      const VkBufferCreateInfo *create_info,
                                      const char *name, TbHostBuffer *buffer) {
  VmaAllocator vma_alloc = self->vma_alloc;

  VmaAllocationCreateInfo alloc_create_info = {
      .memoryTypeBits = VMA_MEMORY_USAGE_CPU_TO_GPU,
      .pool = self->host_buffer_pool,
  };
  VkResult err =
      vmaCreateBuffer(vma_alloc, create_info, &alloc_create_info,
                      &buffer->buffer, &buffer->alloc, &buffer->info);
  TB_VK_CHECK_RET(err, "Failed to allocate host buffer", err);
  SET_VK_NAME(self->render_thread->device, buffer->buffer,
              VK_OBJECT_TYPE_BUFFER, name);

  vmaMapMemory(vma_alloc, buffer->alloc, &buffer->ptr);

  buffer->offset = 0;

  return VK_SUCCESS;
}

VkResult tb_rnd_sys_alloc_gpu_buffer(RenderSystem *self,
                                     const VkBufferCreateInfo *create_info,
                                     const char *name, TbBuffer *buffer) {
  VmaAllocator vma_alloc = self->vma_alloc;
  VmaPool pool = self->gpu_buffer_pool;

  VmaAllocationCreateInfo alloc_create_info = {
      .memoryTypeBits = VMA_MEMORY_USAGE_GPU_ONLY,
      .pool = pool,
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
                                    const char *name, TbImage *image) {
  VmaAllocator vma_alloc = self->vma_alloc;
  VmaPool pool = self->gpu_image_pool;

  VmaAllocationCreateInfo alloc_create_info = {
      .memoryTypeBits = VMA_MEMORY_USAGE_GPU_ONLY,
      .pool = pool,
  };
  VkResult err = vmaCreateImage(vma_alloc, create_info, &alloc_create_info,
                                &image->image, &image->alloc, &image->info);
  TB_VK_CHECK_RET(err, "Failed to allocate gpu image", err);

  SET_VK_NAME(self->render_thread->device, image->image, VK_OBJECT_TYPE_IMAGE,
              name);

  return err;
}

VkBuffer tb_rnd_get_gpu_tmp_buffer(RenderSystem *self) {
  return self->render_thread->frame_states[self->frame_idx].tmp_gpu_buffer;
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
