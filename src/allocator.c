#include "allocator.h"

#include <assert.h>
#include <string.h>

#ifdef __SWITCH__
#include <malloc.h>
#define mi_heap_t int

#define mi_heap_new(...) 0
#define mi_heap_destroy(...) 0
#define mi_heap_delete(...) 0

#define mi_heap_calloc(heap, count, size) calloc(count, size);
#define mi_heap_recalloc(heap, ptr, count, size) realloc(ptr, size)
#define mi_heap_recalloc_aligned(heap, ptr, count, size, alignment)            \
  realloc(ptr, size)

#define mi_free(ptr) free(ptr)
#else
#include <mimalloc.h>
#endif

#include "profiling.h"

void *arena_alloc(void *user_data, size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  ArenaAllocator *arena = (ArenaAllocator *)user_data;
  size_t cur_size = arena->size;
  if (cur_size + size >= arena->max_size) {
    arena->grow = true; // Signal that on the next reset we need to actually do
                        // a resize as the arena is unable to meet demand
    assert(false);
    return NULL;
  }
  void *ptr = &arena->data[cur_size];
  arena->size += size;
  TracyCZoneEnd(ctx);
  return ptr;
}

void *arena_realloc(void *user_data, void *original, size_t size) {
  // In the arena allocator we're not going to bother to really implement
  // realloc for now...
  (void)original;
  return arena_alloc(user_data, size);
}

void *arena_realloc_aligned(void *user_data, void *original, size_t size,
                            size_t alignment) {
  // In the arena allocator we're not going to bother to really implement
  // realloc for now...
  (void)original;
  (void)alignment;
  return arena_alloc(user_data, size);
}

void arena_free(void *user_data, void *ptr) {
  // Do nothing, the arena will reset
  (void)user_data;
  (void)ptr;
}

void create_arena_allocator(ArenaAllocator *a, size_t max_size) {
  mi_heap_t *heap = mi_heap_new();
  // assert(heap); switch doesn't like this
  void *data = mi_heap_calloc(heap, 1, max_size);
  TracyCAllocN(data, max_size, "Arena");
  assert(data);

  (*a) = (ArenaAllocator){
      .max_size = max_size,
      .heap = heap,
      .data = data,
      .alloc =
          (Allocator){
              .alloc = arena_alloc,
              .realloc = arena_realloc,
              .realloc_aligned = arena_realloc_aligned,
              .free = arena_free,
              .user_data = a,
          },
      .grow = false,
  };
}

void reset_arena(ArenaAllocator a, bool allow_grow) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  if (allow_grow && a.grow) {
    a.max_size *= 2;

    a.grow = false;
    TracyCFreeN(a.data, "Arena");
    a.data = mi_heap_recalloc(a.heap, a.data, 1, a.max_size);
    TracyCAllocN(a.data, a.max_size, "Arena");
  }

  a.size = 0;

  assert(a.data);
  TracyCZoneEnd(ctx);
}

void destroy_arena_allocator(ArenaAllocator a) {
  TracyCFreeN(a.data, "Arena");
  mi_free(a.data);
  mi_heap_destroy(a.heap);
}

void *standard_alloc(void *user_data, size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  StandardAllocator *alloc = (StandardAllocator *)user_data;
  void *ptr = mi_heap_calloc(alloc->heap, 1, size);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

void *standard_realloc(void *user_data, void *original, size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  StandardAllocator *alloc = (StandardAllocator *)user_data;
  TracyCFreeN(original, alloc->name);
  void *ptr = mi_heap_recalloc(alloc->heap, original, 1, size);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

void *standard_realloc_aligned(void *user_data, void *original, size_t size,
                               size_t alignment) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  StandardAllocator *alloc = (StandardAllocator *)user_data;
  TracyCFreeN(original, alloc->name);
  void *ptr =
      mi_heap_recalloc_aligned(alloc->heap, original, 1, size, alignment);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

void standard_free(void *user_data, void *ptr) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  StandardAllocator *alloc = (StandardAllocator *)user_data;
  (void)alloc;
  TracyCFreeN(ptr, alloc->name);
  mi_free(ptr);
  TracyCZoneEnd(ctx);
}

void create_standard_allocator(StandardAllocator *a, const char *name) {
  (*a) = (StandardAllocator){
      .heap = mi_heap_new(),
      .alloc =
          {
              .alloc = standard_alloc,
              .realloc = standard_realloc,
              .realloc_aligned = standard_realloc_aligned,
              .free = standard_free,
              .user_data = a,
          },
      .name = name,
  };
}

void destroy_standard_allocator(StandardAllocator a) { mi_heap_delete(a.heap); }
