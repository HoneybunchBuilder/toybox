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
    SDL_RWLock *lock;                                                          \
  }

#define TB_QUEUE_RESET(queue, allocator, cap)                                  \
  {                                                                            \
    TB_DYN_ARR_RESET((queue).storage, (allocator), (cap));                     \
    if ((queue).lock == NULL) {                                                \
      (queue).lock = SDL_CreateRWLock();                                       \
    }                                                                          \
  }

#define TB_QUEUE_DESTROY(queue)                                                \
  {                                                                            \
    TB_DYN_ARR_DESTROY((queue).storage);                                       \
    SDL_DestroyRWLock((queue).lock);                                           \
  }

#define TB_QUEUE_PUSH(queue, element)                                          \
  if (SDL_TryLockRWLockForWriting((queue).lock)) {                             \
    TB_DYN_ARR_APPEND((queue).storage, element);                               \
    SDL_UnlockRWLock((queue).lock);                                            \
  }

#define TB_QUEUE_PUSH_PTR(queue, element)                                      \
  if (SDL_TryLockRWLockForWriting((queue)->lock)) {                            \
    TB_DYN_ARR_APPEND((queue)->storage, element);                              \
    SDL_UnlockRWLock((queue)->lock);                                           \
  }

#define TB_QUEUE_POP(queue, out)                                               \
  ({                                                                           \
    bool ret = false;                                                          \
    if (SDL_TryLockRWLockForWriting((queue).lock)) {                           \
      if (TB_DYN_ARR_SIZE((queue).storage) > 0) {                              \
        if (out) {                                                             \
          (*out) = *TB_DYN_ARR_BACKPTR((queue).storage);                       \
        }                                                                      \
        TB_DYN_ARR_POP((queue).storage);                                       \
        ret = true;                                                            \
      }                                                                        \
      SDL_UnlockRWLock((queue).lock);                                          \
    }                                                                          \
    ret;                                                                       \
  })

#define TB_QUEUE_CLEAR(queue)                                                  \
  if (SDL_TryLockRWLockForWriting((queue).lock)) {                             \
    while (TB_DYN_ARR_SIZE((queue).storage) > 0) {                             \
      TB_DYN_ARR_POP((queue).storage);                                         \
    }                                                                          \
    SDL_UnlockRWLock((queue).lock);                                            \
  }

#ifdef __cplusplus
}
#endif
