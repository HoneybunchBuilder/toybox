#pragma once

#include "tb_dynarray.h"
#include <SDL3/SDL_mutex.h>

// Disabling this warning with -Wno-gnu-statement-expression
// doesn't seem to work in cmake's target_compile_options
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-statement-expression"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_QUEUE_OF(type)                                                      \
  struct {                                                                     \
    TB_DYN_ARR_OF(type) storage;                                               \
    SDL_Mutex *mutex;                                                          \
  }

#define TB_QUEUE_RESET(queue, allocator, cap)                                  \
  {                                                                            \
    TB_DYN_ARR_RESET((queue).storage, (allocator), (cap));                     \
    if ((queue).mutex == NULL) {                                               \
      (queue).mutex = SDL_CreateMutex();                                       \
    }                                                                          \
  }

#define TB_QUEUE_DESTROY(queue)                                                \
  {                                                                            \
    TB_DYN_ARR_DESTROY((queue).storage);                                       \
    SDL_DestroyMutex((queue).mutex);                                           \
  }

#define TB_QUEUE_PUSH(queue, element)                                          \
  if (SDL_TryLockMutex((queue).mutex) == 0) {                                  \
    TB_DYN_ARR_APPEND((queue).storage, element);                               \
    SDL_UnlockMutex((queue).mutex);                                            \
  }

#define TB_QUEUE_PUSH_PTR(queue, element)                                      \
  if (SDL_TryLockMutex((queue)->mutex) == 0) {                                 \
    TB_DYN_ARR_APPEND((queue)->storage, element);                              \
    SDL_UnlockMutex((queue)->mutex);                                           \
  }

#define TB_QUEUE_POP(queue, out)                                               \
  ({                                                                           \
    bool ret = false;                                                          \
    if (SDL_TryLockMutex((queue).mutex) == 0) {                                \
      if (TB_DYN_ARR_SIZE((queue).storage) > 0) {                              \
        if (out) {                                                             \
          (*out) = *TB_DYN_ARR_BACKPTR((queue).storage);                       \
        }                                                                      \
        TB_DYN_ARR_POP((queue).storage);                                       \
        ret = true;                                                            \
      }                                                                        \
      SDL_UnlockMutex((queue).mutex);                                          \
    }                                                                          \
    ret;                                                                       \
  })

#define TB_QUEUE_CLEAR(queue)                                                  \
  if (SDL_TryLockMutex((queue).mutex) == 0) {                                  \
    while (TB_DYN_ARR_SIZE((queue).storage) > 0) {                             \
      TB_DYN_ARR_POP((queue).storage);                                         \
    }                                                                          \
    SDL_UnlockMutex((queue).mutex);                                            \
  }

#ifdef __cplusplus
}
#endif
