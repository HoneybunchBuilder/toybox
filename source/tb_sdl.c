#include "tbsdl.h"

#include "tb_mmap.h"
#include "tbcommon.h"

void *tb_rw_mmap(SDL_RWops *file, size_t size) {
  return tb_mmap(0, size, 0, 0, file->hidden.stdio.fp, 0);
}

void tb_rw_munmap(void *data, size_t size) { tb_munmap(data, size); }
