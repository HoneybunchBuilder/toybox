#pragma once

// Nvidia recommended sizings are 64/126
// https://jcgt.org/published/0012/02/01/paper-lowres.pdf Known to be unoptimal
// on AMD: https://cohost.org/zeux/post/779129-meshlet-sizing-effic Will try to
// address this later
// 64/64 is recommended for AMD by the meshopt source code
#define TB_MESHLET_MAX_VERTICES 64
#define TB_MESHLET_MAX_TRIANGLES 124

#define TB_MESHLET_THREADS 128

#ifndef TB_SHADER
#include "meshoptimizer.h"
#include <stdint.h>
void tb_get_cluster_sizing(uint32_t *max_verts, uint32_t *max_indices);
#endif
