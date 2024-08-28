#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void *tb_mmap(void *start, size_t length, int32_t prot, int32_t flags,
              void *file, size_t offset);

void tb_munmap(void *addr, size_t length);
