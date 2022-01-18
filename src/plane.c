#include "plane.h"

#include "cpuresources.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

uint32_t aligned(uint32_t size) { return ((size - 1) | 15) + 1; }

// float3 pos, float3 normal, float2 uv
#define PLANE_VERTEX_STRIDE (8 * sizeof(float))

void calc_subdiv(uint32_t subdiv, uint32_t *out_index_count,
                 uint32_t *out_vertex_count) {
  uint32_t dimension = subdiv + 1;

  uint32_t face_count = (uint32_t)(pow((double)dimension, 2.0) + 0.5);
  uint32_t triangle_count = face_count * 2;

  uint32_t index_count = 3 * triangle_count;

  // vertex_count = (dimension + 1)^2
  uint32_t vertex_count = dimension + 1;
  vertex_count *= vertex_count;

  *out_index_count = index_count;
  *out_vertex_count = vertex_count;
}

size_t plane_alloc_size(uint32_t subdiv) {
  uint32_t index_count = 0;
  uint32_t vertex_count = 0;
  calc_subdiv(subdiv, &index_count, &vertex_count);

  size_t index_size = aligned(index_count * sizeof(uint16_t));
  size_t vertex_size = aligned(vertex_count * PLANE_VERTEX_STRIDE);

  return sizeof(CPUMesh) + index_size + vertex_size;
}
void create_plane(uint32_t subdiv, CPUMesh *plane) {
  uint32_t index_count = 0;
  uint32_t vertex_count = 0;
  calc_subdiv(subdiv, &index_count, &vertex_count);
  uint32_t dimension = subdiv + 1;
  uint32_t width = dimension + 1;

  size_t index_size = aligned(index_count * sizeof(uint16_t));
  size_t geom_size = aligned(vertex_count * PLANE_VERTEX_STRIDE);

  size_t offset = sizeof(CPUMesh);
  plane->indices = (uint16_t *)(((uint8_t *)plane) + offset);
  offset += index_size;
  plane->vertices = (((uint8_t *)plane) + offset);

  uint16_t *indices = (uint16_t *)plane->indices;
  float *verts = (float *)plane->vertices;

  plane->index_size = index_size;
  plane->geom_size = geom_size;
  plane->index_count = index_count;
  plane->vertex_count = vertex_count;

  // Generate indices
  {
    // Ensure a clean number of triangles and indices
    assert(index_count % 3 == 0);
    assert(index_count % dimension == 0);

    uint32_t idx = 0;
    uint32_t row = 0;
    uint32_t col = 0;
    uint32_t tl, tr, bl, br;
    tl = tr = bl = br = 0;

    for (uint32_t j = 1; j <= dimension; ++j) {
      for (uint32_t i = 1; i <= dimension; ++i) {
        tl = row + (col * width);
        bl = row + ((col + 1) * width);
        tr = (row + 1) + (col * width);
        br = (row + 1) + ((col + 1) * width);

        indices[idx + 0] = tl;
        indices[idx + 1] = bl;
        indices[idx + 2] = tr;
        indices[idx + 3] = tr;
        indices[idx + 4] = bl;
        indices[idx + 5] = br;
        idx += 6;
        col++;
      }
      row++;
      col = 0;
    }
    assert(idx == index_count);
  }

  // Generate vertices
  {
    const float step = 1.0f / (width - 1);

    const float pos_start = -0.5f;
    const float uv_start = 0.0f;

    uint32_t i = 0;

    float x_step = 0;
    float z_step = 0;

    for (uint32_t z = 0; z < width; ++z) {
      for (uint32_t x = 0; x < width; ++x) {
        x_step = x * step;
        z_step = z * step;

        // Positions
        verts[i + 0] = pos_start + x_step;
        verts[i + 2] = pos_start + z_step;

        // normals
        verts[i + 4] = 1.0f;

        // uvs
        verts[i + 6] = uv_start + x_step;
        verts[i + 7] = uv_start + z_step;

        i += 8;
      }
    }
  }

  return;
}
