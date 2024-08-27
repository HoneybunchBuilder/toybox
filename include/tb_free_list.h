#pragma once

#include "tb_dynarray.h"

typedef TB_DYN_ARR_OF(uint32_t) TbFreeList;

void tb_reset_free_list(TbAllocator alloc, TbFreeList *free_list,
                        uint32_t capacity);

// Returns true if the index was properly retrieved
// False if the free list was exhausted
bool tb_pull_index(TbFreeList *free_list, uint32_t *out_idx);

void tb_return_index(TbFreeList *free_list, uint32_t idx);

void tb_destroy_free_list(TbFreeList *free_list);
