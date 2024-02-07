#pragma once

#include <SDL3/SDL_stdinc.h>

typedef struct TbWorld TbWorld;

int32_t tb_check_info_mode(int32_t argc, char *const *argv);
void tb_write_info(TbWorld *world);
