#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wbad-function-cast"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"

#include "cgltf.h"

#pragma clang diagnostic pop

// Toybox specific helpers
#include "tb_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif
cgltf_result tb_decompress_buffer_view(TbAllocator alloc,
                                       cgltf_buffer_view *view);
#ifdef __cplusplus
}
#endif
