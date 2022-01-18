#pragma once

#include <stdint.h>

#include "allocator.h"

typedef struct CPUTexture CPUTexture;

void alloc_pattern(Allocator alloc, uint32_t width, uint32_t height,
                   CPUTexture **out);
void create_pattern(uint32_t width, uint32_t height, CPUTexture *out);
