#include "tbvkalloc.h"

#include "mimalloc.h"
#include "profiling.h"
#include "tbvma.h"

void *tb_vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                     VkSystemAllocationScope scope) {
  (void)pUserData;
  (void)scope;

  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);

  // In mimalloc every heap is thread local except for the global heap.
  // When using debugging tools like RenderDoc their injection may cause
  // some vulkan related allocations to be made from a dll's thread. In which
  // case this will crash if trying to alloc from a mimalloc heap. So we use the
  // global heap instead since it doesn't have this limitation.
  void *ptr = mi_malloc_aligned(size, alignment);

  TracyCAllocN(ptr, size, "Vulkan Global Heap");
  TracyCZoneEnd(ctx);
  return ptr;
}

void *tb_vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                       size_t alignment, VkSystemAllocationScope scope) {
  (void)pUserData;
  (void)scope;

  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);

  TracyCFreeN(pOriginal, "Vulkan Global Heap");
  void *ptr = mi_realloc_aligned(pOriginal, size, alignment);

  TracyCAllocN(ptr, size, "Vulkan Global Heap");
  TracyCZoneEnd(ctx);
  return ptr;
}

void tb_vk_free_fn(void *pUserData, void *pMemory) {
  (void)pUserData;

  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);

  TracyCFreeN(pMemory, "Vulkan Global Heap");
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
