#include "allocator.h"

#include <assert.h>
#include <string.h>

#include <SDL2/SDL_assert.h>
#include <mimalloc.h>

#include "profiling.h"

static void *arena_alloc(void *user_data, size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TbArenaAllocator *arena = (TbArenaAllocator *)user_data;
  size_t cur_size = arena->size;
  if (cur_size + size >= arena->max_size) {
    arena->grow = true; // Signal that on the next reset we need to actually do
                        // a resize as the arena is unable to meet demand
    assert(false);
    return NULL;
  }
  void *ptr = &arena->data[cur_size];

  // Always 16 byte aligned
  intptr_t padding = 0;
  if ((intptr_t)ptr % 16 != 0) {
    padding = (16 - (intptr_t)ptr % 16);
  }
  ptr = (void *)((intptr_t)ptr + padding); // NOLINT

  SDL_assert((intptr_t)ptr % 16 == 0);

  arena->size += (size + padding);
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *arena_alloc_aligned(void *user_data, size_t size,
                                 size_t alignment) {
  // In the arena allocator we're not going to bother to really implement
  // realloc for now...
  (void)alignment;
  return arena_alloc(user_data, size);
}

static void *arena_realloc(void *user_data, void *original, size_t size) {
  // In the arena allocator we're not going to bother to really implement
  // realloc for now...
  (void)original;
  return arena_alloc(user_data, size);
}

static void *arena_realloc_aligned(void *user_data, void *original, size_t size,
                                   size_t alignment) {
  // In the arena allocator we're not going to bother to really implement
  // realloc for now...
  (void)original;
  (void)alignment;
  return arena_alloc(user_data, size);
}

static void arena_free(void *user_data, void *ptr) {
  // Do nothing, the arena will reset
  (void)user_data;
  (void)ptr;
}

void tb_create_arena_alloc(const char *name, TbArenaAllocator *a,
                           size_t max_size) {
  mi_heap_t *heap = mi_heap_new();
  // assert(heap); switch doesn't like this
  void *data = mi_heap_recalloc(heap, NULL, 1, max_size);
  TracyCAllocN(data, max_size, name);
  assert(data);

  (*a) = (TbArenaAllocator){
      .name = name,
      .max_size = max_size,
      .heap = heap,
      .data = data,
      .alloc =
          (TbAllocator){
              .alloc = arena_alloc,
              .alloc_aligned = arena_alloc_aligned,
              .realloc = arena_realloc,
              .realloc_aligned = arena_realloc_aligned,
              .free = arena_free,
              .user_data = a,
          },
      .grow = false,
  };
}

TbArenaAllocator tb_reset_arena(TbArenaAllocator a, bool allow_grow) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  if (allow_grow && a.grow) {
    a.max_size *= 2;

    a.grow = false;
    TracyCFreeN(a.data, a.name);
    a.data = mi_heap_recalloc(a.heap, a.data, 1, a.max_size);
    TracyCAllocN(a.data, a.max_size, a.name);
  }

  a.size = 0;

  assert(a.data);
  TracyCZoneEnd(ctx);
  return a;
}

void tb_destroy_arena_alloc(TbArenaAllocator a) {
  TracyCFreeN(a.data, a.name);
  mi_free(a.data);
  mi_heap_destroy(a.heap);
}

static void *standard_alloc(void *user_data, size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TbGeneralAllocator *alloc = (TbGeneralAllocator *)user_data;
  void *ptr = mi_heap_recalloc(alloc->heap, NULL, 1, size);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *standard_alloc_aligned(void *user_data, size_t size,
                                    size_t alignment) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TbGeneralAllocator *alloc = (TbGeneralAllocator *)user_data;
  void *ptr = mi_heap_calloc_aligned(alloc->heap, 1, size, alignment);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *standard_realloc(void *user_data, void *original, size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TbGeneralAllocator *alloc = (TbGeneralAllocator *)user_data;
  TracyCFreeN(original, alloc->name);
  void *ptr = mi_heap_recalloc(alloc->heap, original, 1, size);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *standard_realloc_aligned(void *user_data, void *original,
                                      size_t size, size_t alignment) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TbGeneralAllocator *alloc = (TbGeneralAllocator *)user_data;
  TracyCFreeN(original, alloc->name);
  void *ptr =
      mi_heap_recalloc_aligned(alloc->heap, original, 1, size, alignment);
  TracyCAllocN(ptr, size, alloc->name);
  TracyCZoneEnd(ctx);
  return ptr;
}

static void standard_free(void *user_data, void *ptr) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TbGeneralAllocator *alloc = (TbGeneralAllocator *)user_data;
  (void)alloc;
  TracyCFreeN(ptr, alloc->name);
  mi_free(ptr);
  TracyCZoneEnd(ctx);
}

void tb_create_gen_alloc(TbGeneralAllocator *a, const char *name) {
  (*a) = (TbGeneralAllocator){
      .heap = mi_heap_new(),
      .alloc =
          {
              .alloc = standard_alloc,
              .alloc_aligned = standard_alloc_aligned,
              .realloc = standard_realloc,
              .realloc_aligned = standard_realloc_aligned,
              .free = standard_free,
              .user_data = a,
          },
      .name = name,
  };
}

void tb_destroy_gen_alloc(TbGeneralAllocator a) { mi_heap_delete(a.heap); }
