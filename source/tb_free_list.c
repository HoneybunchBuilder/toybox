#include "tb_free_list.h"
#include "tb_common.h"

void tb_reset_free_list(TbAllocator alloc, TbFreeList *free_list,
                        uint32_t capacity) {
  TB_DYN_ARR_RESET(*free_list, alloc, capacity);
  TB_DYN_ARR_RESERVE(*free_list, capacity);
  // Reverse iter so the last idx we append is 0 which will make sense as the
  // first index to pop
  for (int32_t i = (int32_t)capacity - 1; i > 0; --i) {
    TB_DYN_ARR_APPEND(*free_list, i);
  }
}

bool tb_pull_index(TbFreeList *free_list, uint32_t *out_idx) {
  TB_CHECK(out_idx, "Invalid output pointer");
  if (TB_DYN_ARR_SIZE(*free_list) <= 0) {
    TB_CHECK(false, "Free list exhausted");
    return false;
  }

  *out_idx = *TB_DYN_ARR_BACKPTR(*free_list);
  TB_DYN_ARR_POP(*free_list);
  return true;
}

void tb_return_index(TbFreeList *free_list, uint32_t idx) {
  TB_CHECK(TB_DYN_ARR_SIZE(*free_list) < free_list->capacity,
           "No space to return index to");
  TB_DYN_ARR_APPEND(*free_list, idx);
}

void tb_destroy_free_list(TbFreeList *free_list) {
  TB_DYN_ARR_DESTROY(*free_list);
}
