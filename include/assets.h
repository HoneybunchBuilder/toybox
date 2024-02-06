#pragma once

#include "allocator.h"

typedef struct cgltf_data cgltf_data;

char *tb_resolve_asset_path(TbAllocator tmp_alloc, const char *source_name);

cgltf_data *tb_read_glb(TbAllocator gp_alloc, const char *path);
