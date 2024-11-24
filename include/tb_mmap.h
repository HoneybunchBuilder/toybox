#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "tb_engine_config.h"

#if TB_WINDOWS == 1
#define PROT_READ 0x1
#define PROT_WRITE 0x2
/* This flag is only available in WinXP+ */
#ifdef FILE_MAP_EXECUTE
#define PROT_EXEC 0x4
#else
#define PROT_EXEC 0x0
#define FILE_MAP_EXECUTE 0
#endif

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)
#else
#include <sys/mman.h>
#endif

void *tb_mmap(void *start, size_t length, int32_t prot, int32_t flags,
              void *file, size_t offset);

void tb_munmap(void *addr, size_t length);
