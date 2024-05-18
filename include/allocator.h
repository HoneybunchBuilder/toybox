#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mimalloc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_heap_s mi_heap_t;

typedef void *tb_alloc_fn(void *user_data, size_t size);
typedef void *tb_alloc_aligned_fn(void *user_data, size_t size,
                                  size_t alignment);
typedef void *tb_realloc_fn(void *user_data, void *original, size_t size);
typedef void *tb_realloc_aligned_fn(void *user_data, void *original,
                                    size_t size, size_t alignment);
typedef void tb_free_fn(void *user_data, void *ptr);

#define tb_alloc(a, size) (a).alloc((a).user_data, (size))
#define tb_alloc_tp(a, T) (T *)(a).alloc((a).user_data, sizeof(T))
#define tb_alloc_nm_tp(a, n, T) (T *)(a).alloc((a).user_data, n * sizeof(T))
#define tb_alloc_aligned(a, size, align)                                       \
  (a).alloc_aligned((a).user_data, (size), (align));
#define tb_realloc(a, orig, size) (a).realloc((a).user_data, (orig), (size))
#define tb_realloc_tp(a, orig, T)                                              \
  (T *)(a).realloc((a).user_data, (orig), sizeof(T))
#define tb_realloc_nm_tp(a, orig, n, T)                                        \
  (T *)(a).realloc((a).user_data, (orig), (n) * sizeof(T))
#define tb_realloc_aligned(a, orig, size, align)                               \
  (a).realloc_aligned((a).user_data, (orig), (size), (align))
#define tb_free(a, ptr) (a).free((a).user_data, (ptr))

typedef struct TbAllocator {
  void *user_data;
  tb_alloc_fn *alloc;
  tb_alloc_aligned_fn *alloc_aligned;
  tb_realloc_fn *realloc;
  tb_realloc_aligned_fn *realloc_aligned;
  tb_free_fn *free;
} TbAllocator;

extern TbAllocator tb_global_alloc;

extern _Thread_local TbAllocator tb_thread_alloc;

typedef struct TbGeneralAllocator {
  mi_heap_t *heap;
  TbAllocator alloc;
  const char *name;
} TbGeneralAllocator;

void tb_create_gen_alloc(TbGeneralAllocator *a, const char *name);
void tb_destroy_gen_alloc(TbGeneralAllocator a);

// An arena allocator is a type of unmanaged allocator
// You can make allocations but you have no control over when those are freed
// In this case the arena is freed whenever tb_reset_arena is called
typedef struct TbArenaAllocator {
  const char *name;
  mi_heap_t *heap;
  size_t size;
  size_t max_size;
  uint8_t *data;
  TbAllocator alloc;
  bool grow;
} TbArenaAllocator;

void tb_create_arena_alloc(const char *name, TbArenaAllocator *a,
                           size_t max_size);
TbArenaAllocator tb_reset_arena(TbArenaAllocator a, bool allow_grow);
void tb_destroy_arena_alloc(TbArenaAllocator a);

#ifdef __cplusplus
}
#endif
