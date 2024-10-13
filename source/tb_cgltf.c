#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wextra-semi-stmt"

#define CGLTF_IMPLEMENTATION
#include "tb_gltf.h"

#pragma clang diagnostic pop

#include "tb_common.h"
#include "tb_profiling.h"
#include "tb_sdl.h"

#include <meshoptimizer.h>

// Based on an example from this cgltf commit message:
// https://github.com/jkuhlmann/cgltf/commit/bd8bd2c9cc08ff9b75a9aa9f99091f7144665c60
cgltf_result tb_decompress_buffer_view(TbAllocator alloc,
                                       cgltf_buffer_view *view) {
  TB_TRACY_SCOPE("Decompress Buffer");
  if (view->data != NULL) {
    // Already decoded
    return cgltf_result_success;
  }

  // Uncompressed buffer? No problem
  if (!view->has_meshopt_compression) {
    uint8_t *data = (uint8_t *)view->buffer->data;
    data += view->offset;

    uint8_t *result = tb_alloc(alloc, view->size);
    SDL_memcpy(result, data, view->size); // NOLINT
    view->data = result;
    TB_LOG_INFO(SDL_LOG_CATEGORY_SYSTEM, "%s", "Using Uncompressed Buffer");
    return cgltf_result_success;
  }

  const cgltf_meshopt_compression *mc = &view->meshopt_compression;
  uint8_t *data = (uint8_t *)mc->buffer->data;
  data += mc->offset;
  TB_CHECK_RETURN(data, "Invalid data", cgltf_result_invalid_gltf);

  uint8_t *result = tb_alloc(alloc, mc->count * mc->stride);
  TB_CHECK_RETURN(result, "Failed to allocate space for decoded buffer view",
                  cgltf_result_out_of_memory);

  {
    TB_TRACY_SCOPE("Decoding");
    int32_t res = -1;
    switch (mc->mode) {
    default:
    case cgltf_meshopt_compression_mode_invalid:
      break;

    case cgltf_meshopt_compression_mode_attributes:
      res = meshopt_decodeVertexBuffer(result, mc->count, mc->stride, data,
                                       mc->size);
      break;

    case cgltf_meshopt_compression_mode_triangles:
      res = meshopt_decodeIndexBuffer(result, mc->count, mc->stride, data,
                                      mc->size);
      break;

    case cgltf_meshopt_compression_mode_indices:
      res = meshopt_decodeIndexSequence(result, mc->count, mc->stride, data,
                                        mc->size);
      break;
    }
    TB_CHECK_RETURN(res == 0, "Failed to decode buffer view",
                    cgltf_result_io_error);
  }

  {
    TB_TRACY_SCOPE("Filtering");
    switch (mc->filter) {
    default:
    case cgltf_meshopt_compression_filter_none:
      break;

    case cgltf_meshopt_compression_filter_octahedral:
      meshopt_decodeFilterOct(result, mc->count, mc->stride);
      break;

    case cgltf_meshopt_compression_filter_quaternion:
      meshopt_decodeFilterQuat(result, mc->count, mc->stride);
      break;

    case cgltf_meshopt_compression_filter_exponential:
      meshopt_decodeFilterExp(result, mc->count, mc->stride);
      break;
    }
  }

  view->data = result;
  return cgltf_result_success;
}
