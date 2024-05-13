#include "assets.h"

#include "cgltf.h"
#include "tbcommon.h"

static cgltf_result
sdl_read_glb(const struct cgltf_memory_options *memory_options,
             const struct cgltf_file_options *file_options, const char *path,
             cgltf_size *size, void **data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;
  cgltf_size file_size = (cgltf_size)SDL_RWsize(file);
  (void)path;

  void *mem = memory_options->alloc_func(memory_options->user_data, file_size);
  TB_CHECK_RETURN(mem, "clgtf out of memory.", cgltf_result_out_of_memory);

  size_t err = SDL_RWread(file, mem, file_size);
  TB_CHECK_RETURN(err != 0, "clgtf io error.", cgltf_result_io_error);

  *size = file_size;
  *data = mem;

  return cgltf_result_success;
}

static void sdl_release_glb(const struct cgltf_memory_options *memory_options,
                            const struct cgltf_file_options *file_options,
                            void *data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;

  memory_options->free_func(memory_options->user_data, data);

  bool ok = SDL_RWclose(file) == 0;
  TB_CHECK(ok, "Failed to close glb file.");
}

char *tb_resolve_asset_path(TbAllocator tmp_alloc, const char *source_name) {
  const uint32_t max_asset_len = 2048;
  char *asset_path = tb_alloc(tmp_alloc, max_asset_len);
  SDL_memset(asset_path, 0, max_asset_len); // NOLINT
  SDL_snprintf(asset_path, max_asset_len, "%s%s", ASSET_PREFIX,
               source_name); // NOLINT

  TB_CHECK_RETURN(asset_path, "Failed to resolve asset path.", NULL);
  return asset_path;
}

cgltf_data *tb_read_glb(TbAllocator gp_alloc, const char *path) {
  cgltf_data *data = NULL;

  SDL_RWops *glb_file = SDL_RWFromFile(path, "rb");
  const char *err = SDL_GetError();
  TB_CHECK_RETURN(glb_file, "Failed to open glb.", NULL);

  cgltf_options options = {.type = cgltf_file_type_glb,
                           .memory =
                               {
                                   .user_data = gp_alloc.user_data,
                                   .alloc_func = gp_alloc.alloc,
                                   .free_func = gp_alloc.free,
                               },
                           .file = {
                               .read = sdl_read_glb,
                               .release = sdl_release_glb,
                               .user_data = glb_file,
                           }};

  cgltf_result res = cgltf_parse_file(&options, path, &data);
  TB_CHECK_RETURN(res == cgltf_result_success, "Failed to parse glb.", NULL);

  res = cgltf_load_buffers(&options, data, path);
  TB_CHECK_RETURN(res == cgltf_result_success, "Failed to load glb buffers.",
                  NULL);

#if !defined(FINAL)
  res = cgltf_validate(data);
  TB_CHECK_RETURN(res == cgltf_result_success, "Failed to validate glb.", NULL);
#endif
  return data;
}
