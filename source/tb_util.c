#include "tb_util.h"

uint64_t tb_calc_aligned_size(uint32_t count, uint32_t stride,
                              uint32_t alignment) {
  uint64_t size = count * stride;
  size += (alignment - (size % alignment));
  return size;
}
