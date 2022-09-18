#pragma once

#include <stdint.h>

uint32_t get_skydome_index_count(void);
uint64_t get_skydome_size(void);
uint64_t get_skydome_vert_offset(void);

void copy_skydome(void *dst);
