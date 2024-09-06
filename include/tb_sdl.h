#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wdocumentation-deprecated-sync"
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wdouble-promotion"

#include <SDL3/SDL.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_vulkan.h>

void *tb_io_mmap(SDL_IOStream *file, size_t size);

void tb_io_munmap(void *data, size_t size);

#pragma clang diagnostic pop
