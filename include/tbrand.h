#pragma once

#include <SDL2/SDL_stdinc.h>

void tb_seed(uint64_t seed0, uint64_t seed1);
uint64_t tb_rand(void);
float tb_randf(void);
uint64_t tb_rand_range(uint64_t min, uint64_t max);
float tb_rand_rangef(float min, float max);
