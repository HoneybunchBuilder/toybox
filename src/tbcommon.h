#pragma once

#include "tbsdl.h"

#define TB_CHECK(expr, message)                                                \
  if (!(expr)) {                                                               \
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", (message));               \
    SDL_TriggerBreakpoint();                                                   \
  }
#define TB_CHECK_RETURN(expr, message, ret)                                    \
  if (!(expr)) {                                                               \
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", (message));               \
    SDL_TriggerBreakpoint();                                                   \
    return (ret);                                                              \
  }

#define TB_COPY(dst, src, count, type)                                         \
  SDL_memcpy((dst), (src), sizeof(type) * count)

#ifdef __ANDROID__
#define ASSET_PREFIX ""
#else
#define ASSET_PREFIX "./assets/"
#endif
