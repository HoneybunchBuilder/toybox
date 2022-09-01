#include "tbvkalloc.h"

#include "mimalloc.h"
#include "profiling.h"
#include "tbvma.h"

void *tb_vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                     VkSystemAllocationScope scope) {
  (void)scope;

  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);

  VkHostAlloc *alloc = (VkHostAlloc *)pUserData;
  void *ptr = mi_heap_malloc_aligned(alloc->heap, size, alignment);

  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

void *tb_vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                       size_t alignment, VkSystemAllocationScope scope) {
  (void)scope;

  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);

  VkHostAlloc *alloc = (VkHostAlloc *)pUserData;
  TracyCFreeN(pOriginal, alloc->name);
  void *ptr = mi_heap_realloc_aligned(alloc->heap, pOriginal, size, alignment);

  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

void tb_vk_free_fn(void *pUserData, void *pMemory) {

  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);

  VkHostAlloc *alloc = (VkHostAlloc *)pUserData;
  TracyCFreeN(pMemory, alloc->name);
  mi_free(pMemory);

  TracyCZoneEnd(ctx);
}

void tb_vma_alloc_fn(VmaAllocator allocator, uint32_t memoryType,
                     VkDeviceMemory memory, VkDeviceSize size,
                     void *pUserData) {
  (void)allocator;
  (void)memoryType;
  (void)memory;
  (void)size;
  (void)pUserData;
  TracyCAllocN((void *)memory, size, "VMA")
}
void tb_vma_free_fn(VmaAllocator allocator, uint32_t memoryType,
                    VkDeviceMemory memory, VkDeviceSize size, void *pUserData) {
  (void)allocator;
  (void)memoryType;
  (void)memory;
  (void)size;
  (void)pUserData;
  TracyCFreeN((void *)memory, "VMA")
}
