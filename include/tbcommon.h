#pragma once

#include "tbsdl.h"

#include "allocator.h"
#include "profiling.h"
#include "settings.h"
#include "tblog.h"
#include "shadercommon.h"
#include "simd.h"

// Leaning in to clang language extensions
// __auto_type is really cool
#pragma clang diagnostic ignored "-Wgnu-auto-type"
#define tb_auto __auto_type

#define TB_CHECK(expr, message)                                                \
  if (!(expr)) {                                                               \
    TB_LOG_CRITICAL(SDL_LOG_CATEGORY_APPLICATION, "%s", (message));               \
    SDL_TriggerBreakpoint();                                                   \
  }
#define TB_CHECK_RETURN(expr, message, ret)                                    \
  if (!(expr)) {                                                               \
    TB_LOG_CRITICAL(SDL_LOG_CATEGORY_APPLICATION, "%s", (message));               \
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
