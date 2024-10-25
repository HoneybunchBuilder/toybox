#include "tb_meshlet.h"

// Nvidia recommended sizings are 64/126
// https://jcgt.org/published/0012/02/01/paper-lowres.pdf Known to be unoptimal
// on AMD: https://cohost.org/zeux/post/779129-meshlet-sizing-effic Will try to
// address this later
// 64/64 is recommended for AMD by the meshopt source code
#define TB_MESHLET_MAX_VERTICES 64
#define TB_MESHLET_MAX_TRIANGLES 124

// MAX_TRIANGLES has to be divisible by 4 per mesh_opt assert
static_assert(TB_MESHLET_MAX_TRIANGLES % 4 == 0);

void tb_get_cluster_sizing(uint32_t *max_verts, uint32_t *max_tris) {
  SDL_assert(max_verts);
  SDL_assert(max_tris);
  // TODO: Do something more sophisticated
  *max_verts = TB_MESHLET_MAX_VERTICES;
  *max_tris = TB_MESHLET_MAX_TRIANGLES;
}