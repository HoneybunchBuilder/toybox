#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mi_heap_s mi_heap_t;

typedef void *alloc_fn(void *user_data, size_t size);
typedef void *realloc_fn(void *user_data, void *original, size_t size);
typedef void *realloc_aligned_fn(void *user_data, void *original, size_t size,
                                 size_t alignment);
typedef void free_fn(void *user_data, void *ptr);

#define hb_alloc(a, size) (a).alloc((a).user_data, (size))
#define hb_alloc_tp(a, T) (T *)(a).alloc((a).user_data, sizeof(T))
#define hb_alloc_nm_tp(a, n, T) (T *)(a).alloc((a).user_data, n * sizeof(T))
#define hb_realloc(a, orig, size) (a).realloc((a).user_data, (orig), (size))
#define hb_realloc_tp(a, orig, T)                                              \
  (T *)(a).realloc((a).user_data, (orig), sizeof(T))
#define hb_realloc_nm_tp(a, orig, n, T)                                        \
  (T *)(a).realloc((a).user_data, (orig), (n) * sizeof(T))
#define hb_realloc_aligned(a, orig, size, align)                               \
  (a).realloc_aligned((a).user_data, (orig), (size), (align))
#define hb_free(a, ptr) a.free(a.user_data, (ptr))

typedef struct Allocator {
  void *user_data;
  alloc_fn *alloc;
  realloc_fn *realloc;
  realloc_aligned_fn *realloc_aligned;
  free_fn *free;
} Allocator;

typedef struct ArenaAllocator {
  mi_heap_t *heap;
  size_t size;
  size_t max_size;
  uint8_t *data;
  Allocator alloc;
  bool grow;
} ArenaAllocator;

void create_arena_allocator(ArenaAllocator *a, size_t max_size);
ArenaAllocator reset_arena(ArenaAllocator a, bool allow_grow);
void destroy_arena_allocator(ArenaAllocator a);

typedef struct StandardAllocator {
  mi_heap_t *heap;
  Allocator alloc;
  const char *name;
} StandardAllocator;

void create_standard_allocator(StandardAllocator *a, const char *name);
void destroy_standard_allocator(StandardAllocator a);
