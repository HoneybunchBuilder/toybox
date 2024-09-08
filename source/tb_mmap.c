#include "tb_mmap.h"

#include "tb_common.h"

// Adapted from https://github.com/m-labs/uclibc-lm32/ (public domain)

#if TB_WINDOWS == 1

#include <io.h>
#include <sys/types.h>
#include <windows.h>

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

#ifdef __USE_FILE_OFFSET64
#define DWORD_HI(x) (x >> 32)
#define DWORD_LO(x) ((x) & 0xffffffff)
#else
#define DWORD_HI(x) (0)
#define DWORD_LO(x) (x)
#endif

void *tb_mmap(void *start, size_t length, int32_t prot, int32_t flags,
              void *file, size_t offset) {
  (void)start;
  if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
    return MAP_FAILED;
  if ((intptr_t)file == (intptr_t)-1) {
    if (!(flags & MAP_ANON) || offset) {
      return MAP_FAILED;
    }
  } else if (flags & MAP_ANON) {
    return MAP_FAILED;
  }

  DWORD flProtect = PAGE_READONLY;
  if (prot & PROT_WRITE) {
    if (prot & PROT_EXEC) {
      flProtect = PAGE_EXECUTE_READWRITE;
    } else {
      flProtect = PAGE_READWRITE;
    }
  } else if (prot & PROT_EXEC) {
    if (prot & PROT_READ) {
      flProtect = PAGE_EXECUTE_READ;
    } else if (prot & PROT_EXEC) {
      flProtect = PAGE_EXECUTE;
    }
  }

  size_t end = length + offset;
  HANDLE h = CreateFileMapping(file, NULL, flProtect, DWORD_HI(end),
                               DWORD_LO(end), NULL);
  if (h == NULL) {
    return MAP_FAILED;
  }

  DWORD dwDesiredAccess = 0;
  if (prot & PROT_WRITE) {
    dwDesiredAccess = FILE_MAP_WRITE;
  } else {
    dwDesiredAccess = FILE_MAP_READ;
  }
  if (prot & PROT_EXEC) {
    dwDesiredAccess |= FILE_MAP_EXECUTE;
  }
  if (flags & MAP_PRIVATE) {
    dwDesiredAccess |= FILE_MAP_COPY;
  }
  void *ret = MapViewOfFile(h, dwDesiredAccess, DWORD_HI(offset),
                            DWORD_LO(offset), length);
  if (ret == NULL) {
    CloseHandle(h);
    ret = MAP_FAILED;
  }
  return ret;
}

void tb_munmap(void *addr, size_t length) {
  (void)length;
  UnmapViewOfFile(addr);
}

#undef DWORD_HI
#undef DWORD_LO

#else
#include <sys/mman.h>

void *tb_mmap(void *start, size_t length, int32_t prot, int32_t flags,
              void *file, size_t offset) {
  return mmap(start, length, prot, flags, (intptr_t)file, offset);
}

void tb_munmap(void *addr, size_t length) { munmap(addr, length); }

#endif
