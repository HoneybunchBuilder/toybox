#include "tb_mmap.h"

#include "tb_common.h"

// Adapted from https://github.com/m-labs/uclibc-lm32/ (public domain)

#if TB_WINDOWS == 1

#include <io.h>
#include <sys/types.h>
#include <windows.h>

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

void *tb_mmap(void *start, size_t length, int32_t prot, int32_t flags,
              void *file, size_t offset) {
  int32_t file_descriptor = fileno(file);
  void *ret = mmap(start, length, prot, flags, file_descriptor, offset);
#ifndef FINAL
  if (ret == MAP_FAILED) {
    int32_t err = errno;
    TB_CHECK(err, "Error occurred during mmap");
  }
#endif
  return ret;
}

void tb_munmap(void *addr, size_t length) { munmap(addr, length); }

#endif
