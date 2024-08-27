#pragma once

// For misc functions that don't belong anywhere else

#include <SDL3/SDL_stdinc.h>

uint64_t tb_calc_aligned_size(uint32_t count, uint32_t stride,
                              uint32_t alignment);
