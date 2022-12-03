#pragma once

#include "tbvk.h"

typedef struct VmaAllocator_T *VmaAllocator;

void *tb_vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                     VkSystemAllocationScope scope);
void *tb_vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                       size_t alignment, VkSystemAllocationScope scope);
void tb_vk_free_fn(void *pUserData, void *pMemory);

void tb_vma_alloc_fn(VmaAllocator allocator, uint32_t memoryType,
                     VkDeviceMemory memory, VkDeviceSize size, void *pUserData);
void tb_vma_free_fn(VmaAllocator allocator, uint32_t memoryType,
                    VkDeviceMemory memory, VkDeviceSize size, void *pUserData);
