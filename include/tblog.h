#pragma once

#include <SDL3/SDL_log.h>

enum {
  TB_LOG_CATEGORY_RENDER_THREAD = SDL_LOG_CATEGORY_CUSTOM,
  TB_LOG_CATEGORY_CUSTOM,
};

#ifdef FINAL
#define TB_LOG_VERBOSE(category, fmt, ...)
#define TB_LOG_INFO(category, fmt, ...)
#define TB_LOG_DEBUG(category, fmt, ...)
#define TB_LOG_WARN(category, fmt, ...)
#define TB_LOG_ERROR(category, fmt, ...)
#define TB_LOG_CRITICAL(category, fmt, ...)
#else
#define TB_LOG_VERBOSE(category, fmt, ...)                                     \
  SDL_LogVerbose((category), (fmt), __VA_ARGS__)
#define TB_LOG_INFO(category, fmt, ...)                                        \
  SDL_LogInfo((category), (fmt), __VA_ARGS__)
#define TB_LOG_DEBUG(category, fmt, ...)                                       \
  SDL_LogDebug((category), (fmt), __VA_ARGS__)
#define TB_LOG_WARN(category, fmt, ...)                                        \
  SDL_LogWarn((category), (fmt), __VA_ARGS__)
#define TB_LOG_ERROR(category, fmt, ...)                                       \
  SDL_LogError((category), (fmt), __VA_ARGS__)
#define TB_LOG_CRITICAL(category, fmt, ...)                                    \
  SDL_LogCritical((category), (fmt), __VA_ARGS__)
#endif
