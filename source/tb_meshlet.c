#include "tb_meshlet.h"

#include "meshoptimizer.h"
#include "tb_common.h"

// MAX_TRIANGLES has to be divisible by 4 per mesh_opt assert
static_assert(TB_MESHLET_MAX_TRIANGLES % 4 == 0);

void tb_get_cluster_sizing(uint32_t *max_verts, uint32_t *max_tris) {
  SDL_assert(max_verts);
  SDL_assert(max_tris);
  // TODO: Do something more sophisticated
  *max_verts = TB_MESHLET_MAX_VERTICES;
  *max_tris = TB_MESHLET_MAX_TRIANGLES;
}
