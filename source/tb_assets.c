#include "tb_assets.h"

#include "cgltf.h"
#include "tb_common.h"
#include "tb_mmap.h"

static cgltf_result
sdl_read_glb(const struct cgltf_memory_options *memory_options,
             const struct cgltf_file_options *file_options, const char *path,
             cgltf_size *size, void **data) {
  (void)memory_options;
  (void)path;
  tb_auto file = (SDL_RWops *)file_options->user_data;
  tb_auto file_size = (cgltf_size)SDL_RWsize(file);

  *data = tb_rw_mmap(file, file_size);
  *size = file_size;

  return cgltf_result_success;
}

static void sdl_release_glb(const struct cgltf_memory_options *memory_options,
                            const struct cgltf_file_options *file_options,
                            void *data) {
  (void)memory_options;

  tb_auto file = (SDL_RWops *)file_options->user_data;
  tb_auto file_size = (cgltf_size)SDL_RWsize(file);
  tb_rw_munmap(data, file_size);

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

#if !defined(TB_FINAL)
  res = cgltf_validate(data);
  TB_CHECK_RETURN(res == cgltf_result_success, "Failed to validate glb.", NULL);
#endif

  return data;
}
