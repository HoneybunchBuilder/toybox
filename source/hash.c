#include "hash.h"

uint64_t tb_hash(uint64_t hash, const uint8_t *data, uint64_t len) {
  int32_t c;

  for (uint32_t i = 0; i < len; ++i) {
    c = data[i];
    hash = c + (hash << 6) + (hash << 16) - hash;
  }

  return hash;
}
