#pragma once

// Adapted from
// https://gist.github.com/nicebyte/86bd1f119d3ff5c8da06bc2fd59ad668

#include <assert.h>

#include "allocator.h"

#define TB_DYN_ARR_OF(type)                                                    \
  struct {                                                                     \
    TbAllocator alloc;                                                         \
    type *data;                                                                \
    type *endptr;                                                              \
    uint32_t capacity;                                                         \
  }

#if !defined(__cplusplus)
#define decltype(x) void *
#endif

#define TB_DYN_ARR_RESET(array, allocator, cap)                                \
  {                                                                            \
    assert(cap);                                                               \
    array.alloc = allocator;                                                   \
    array.data = (decltype(array.data))tb_alloc(allocator,                     \
                                                sizeof(array.data[0]) * cap);  \
    array.endptr = array.data;                                                 \
    array.capacity = cap;                                                      \
  }

#define TB_DYN_ARR_RESIZE(a, s)                                                \
  {                                                                            \
    uint32_t size = (s);                                                       \
    if ((a).capacity < size) {                                                 \
      (a).data = (decltype((a).data))tb_realloc((a).alloc, (a).data,           \
                                                sizeof((a).data[0]) * size);   \
      (a).capacity = size;                                                     \
    }                                                                          \
    (a).endptr = (a).data + size;                                              \
  }

#define TB_DYN_ARR_RESERVE(a, c)                                               \
  {                                                                            \
    uint32_t cap = (c);                                                        \
    if ((a).capacity < cap) {                                                  \
      ptrdiff_t cur_size = (a).endptr - (a).data;                              \
      (a).data = (decltype((a).data))tb_realloc((a).alloc, (a).data,           \
                                                sizeof((a).data[0]) * cap);    \
      (a).capacity = cap;                                                      \
      (a).endptr = (a).data + cur_size;                                        \
    }                                                                          \
  }

#define TB_DYN_ARR_DESTROY(a)                                                  \
  if ((a).data != NULL) {                                                      \
    tb_free((a).alloc, (a).data);                                              \
    (a).data = (a).endptr = NULL;                                              \
  }

#define TB_DYN_ARR_APPEND(a, v)                                                \
  {                                                                            \
    ptrdiff_t cur_size = (a).endptr - (a).data;                                \
    assert(cur_size >= 0);                                                     \
    if ((size_t)cur_size >= (a).capacity) {                                    \
      (a).capacity <<= 1u;                                                     \
      decltype((a).data) tmp = (decltype((a).data))tb_realloc(                 \
          (a).alloc, (a).data, sizeof((a).data[0]) * (a).capacity);            \
      assert(tmp != NULL);                                                     \
      (a).data = tmp;                                                          \
      (a).endptr = &(a).data[cur_size];                                        \
    }                                                                          \
    *((a).endptr++) = (v);                                                     \
  }

#define TB_DYN_ARR_CLEAR(a) ((a).endptr = (a).data)
#define TB_DYN_ARR_SIZE(a) ((uint32_t)((a).endptr - (a).data))
#define TB_DYN_ARR_AT(a, i) ((a).data[i])

#define TB_DYN_ARR_POP(a)                                                      \
  {                                                                            \
    assert((a).data != (a).endptr);                                            \
    --((a).endptr);                                                            \
  }

#define TB_DYN_ARR_EMPTY(a) ((a).endptr == (a).data)

#define TB_DYN_ARR_BACKPTR(a) ((a).endptr - 1)

#define TB_DYN_ARR_FOREACH(a, countername)                                     \
  for (size_t countername = 0; (countername) < TB_DYN_ARR_SIZE((a));           \
       ++(countername))

/*
 Example usage:
  typedef struct point { uint32_t x, y } point;
  void foo() {
    TB_DYN_ARR_OF(point) points;
    TB_DYN_ARR_RESET(alloc, points, 100u);
    uint32_t npoints = 200u;
    for (uint32_t i = 0u; i < npoints; ++i) {
      point p = {i, i * 10u};
      TB_DYN_ARR_APPEND(points, p);
    }
    assert(TB_DYN_ARR_SIZE(points) == 200u);

    TB_DYN_ARR_FOREACH(points, i) {
      assert(TB_DYN_ARR_AT(points, i).x == i);
      assert(TB_DYN_ARR_AT(points, i).y == i * 10u);
    }
    TB_DYN_ARR_CLEAR(points);
    assert(TB_DYN_ARR_SIZE(points) == 0u);
    TB_DYN_ARR_DESTROY(points);
  }
*/
