#pragma once

#include "dynarray.h"
#include <SDL3/SDL_Mutex.h>

#define TB_QUEUE_OF(type)                                                      \
  struct {                                                                     \
    TB_DYN_ARR_OF(type) storage;                                               \
    SDL_Mutex *mutex;                                                          \
  }

#define TB_QUEUE_RESET(queue, allocator, cap)                                  \
  {                                                                            \
    TB_DYN_ARR_RESET((queue).storage, (allocator), (cap));                     \
    if (queue.mutex == NULL) {                                                 \
      queue.mutex = SDL_CreateMutex();                                         \
    }                                                                          \
  }

#define TB_QUEUE_DESTROY(queue)                                                \
  {                                                                            \
    TB_DYN_ARR_DESTROY(queue.storage);                                         \
    SDL_DestroyMutex(queue.mutex);                                             \
  }

#define TB_QUEUE_PUSH(queue, element)                                          \
  if (SDL_TryLockMutex(queue.mutex) == 0) {                                    \
    TB_DYN_ARR_APPEND(queue.storage, element);                                 \
    SDL_UnlockMutex(queue.mutex);                                              \
  }

#define TB_QUEUE_POP(queue, out)                                               \
  ({                                                                           \
    _Pragma("clang diagnostic push");                                          \
    _Pragma("clang diagnostic ignored \"-Wgnu-statement-expression\"");        \
    bool ret = false;                                                          \
    if (SDL_TryLockMutex(queue.mutex) == 0) {                                  \
      (*out) = *TB_DYN_ARR_BACKPTR(queue.storage);                             \
      TB_DYN_ARR_POP(queue.storage);                                           \
      ret = true;                                                              \
    }                                                                          \
    _Pragma("clang diagnostic pop");                                           \
    ret;                                                                       \
  })

#define TB_QUEUE_CLEAR(queue)                                                  \
  while (TB_QUEUE_POP(queue)) {                                                \
  }
