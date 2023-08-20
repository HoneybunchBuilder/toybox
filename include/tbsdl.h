#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wdocumentation-deprecated-sync"
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_rwops.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_vulkan.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifndef FINAL
#define TB_LOG(verbosity, category, fmt)                                       \
  SDL_Log##verbosity(SDL_LOG_CATEGORY_##category, fmt)
#define TB_LOG_VA(verbosity, category, fmt, ...)                               \
  SDL_Log##verbosity(SDL_LOG_CATEGORY_##category, fmt, __VA_ARGS__)
#else
#define TB_LOG(verbosity, category, fmt)
#define TB_LOG_VA(verbosity, category, fmt, ...)
#endif
