#pragma once

#include "tbsdl.h"

#include "profiling.h"
#include "settings.h"
#include "shadercommon.h"
#include "simd.h"
#include "tb_allocator.h"
#include "tbengineconfig.h"
#include "tblog.h"

// Manually definine preprocessor macros to reduce windows header includes
// https://aras-p.info/blog/2018/01/12/Minimizing-windows.h/
#if TB_WINDOWS == 1
#define WIN32_LEAN_AND_MEAN
#if TB_X64 == 1
#define _AMD64_
#elif TB_ARM64 = 1
#define _ARM_
#endif
#endif

// Leaning in to clang language extensions
// __auto_type is really cool
#pragma clang diagnostic ignored "-Wgnu-auto-type"
#define tb_auto __auto_type

#define TB_CHECK(expr, message)                                                \
  if (!(expr)) {                                                               \
    TB_LOG_CRITICAL(SDL_LOG_CATEGORY_APPLICATION, "%s", (message));            \
    SDL_TriggerBreakpoint();                                                   \
  }
#define TB_CHECK_RETURN(expr, message, ret)                                    \
  if (!(expr)) {                                                               \
    TB_LOG_CRITICAL(SDL_LOG_CATEGORY_APPLICATION, "%s", (message));            \
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
