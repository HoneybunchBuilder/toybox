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

#ifdef __ANDROID__
#define ASSET_PREFIX ""
#else
#define ASSET_PREFIX "./assets/"
#endif
