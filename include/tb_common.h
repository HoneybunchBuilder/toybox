#pragma once

#include "tb_sdl.h"

#include "tb_allocator.h"
#include "tb_engine_config.h"
#include "tb_log.h"
#include "tb_profiling.h"
#include "tb_settings.h"
#include "tb_simd.h"

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

// We don't use the problematic parts of VLAs, we just want const variables to
// define array length
#pragma clang diagnostic ignored "-Wgnu-folding-constant"

// Flecs uses '$' in identifiers as part of a DSL so this gets in the way
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"

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

#ifdef __ANDROID__
#define ASSET_PREFIX ""
#else
#define ASSET_PREFIX "./assets/"
#endif
