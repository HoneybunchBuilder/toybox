#include "tb_sdl.h"

#include "tb_common.h"
#include "tb_mmap.h"

void *tb_io_mmap(SDL_IOStream *file, size_t size) {
  tb_auto props = SDL_GetIOProperties(file);

#if TB_WINDOWS == 1
  const char *prop_name = SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER;
#else
  const char *prop_name = SDL_PROP_IOSTREAM_STDIO_FILE_POINTER;
#endif
  void *file_handle = SDL_GetPointerProperty(props, prop_name, NULL);
  return tb_mmap(0, size, 0, 0, file_handle, 0);
}

void tb_io_munmap(void *data, size_t size) { tb_munmap(data, size); }
