#include "cube.h"
#include "cpuresources.h"

#include <string.h>

static const uint16_t cube_indices[] = {
    0,  1,  2,  2,  1,  3,  // Front
    4,  5,  6,  6,  5,  7,  // Back
    8,  9,  10, 10, 9,  11, // Right
    12, 13, 14, 14, 13, 15, // Left
    16, 17, 18, 18, 17, 19, // Top
    20, 21, 22, 22, 21, 23, // Bottom
};

static const float3 cube_positions[] = {
    // front
    {-1.0f, -1.0f, -1.0f}, // point blue
    {+1.0f, -1.0f, -1.0f}, // point magenta
    {-1.0f, +1.0f, -1.0f}, // point cyan
    {+1.0f, +1.0f, -1.0f}, // point white
    // back
    {+1.0f, -1.0f, +1.0f}, // point red
    {-1.0f, -1.0f, +1.0f}, // point black
    {+1.0f, +1.0f, +1.0f}, // point yellow
    {-1.0f, +1.0f, +1.0f}, // point green
    // right
    {+1.0f, -1.0f, -1.0f}, // point magenta
    {+1.0f, -1.0f, +1.0f}, // point red
    {+1.0f, +1.0f, -1.0f}, // point white
    {+1.0f, +1.0f, +1.0f}, // point yellow
    // left
    {-1.0f, -1.0f, +1.0f}, // point black
    {-1.0f, -1.0f, -1.0f}, // point blue
    {-1.0f, +1.0f, +1.0f}, // point green
    {-1.0f, +1.0f, -1.0f}, // point cyan
    // top
    {-1.0f, +1.0f, -1.0f}, // point cyan
    {+1.0f, +1.0f, -1.0f}, // point white
    {-1.0f, +1.0f, +1.0f}, // point green
    {+1.0f, +1.0f, +1.0f}, // point yellow
    // bottom
    {-1.0f, -1.0f, +1.0f}, // point black
    {+1.0f, -1.0f, +1.0f}, // point red
    {-1.0f, -1.0f, -1.0f}, // point blue
    {+1.0f, -1.0f, -1.0f}  // point magenta
};

static const float3 cube_colors[] = {
    // front
    {0.0f, 0.0f, 1.0f}, // blue
    {1.0f, 0.0f, 1.0f}, // magenta
    {0.0f, 1.0f, 1.0f}, // cyan
    {1.0f, 1.0f, 1.0f}, // white
    // back
    {1.0f, 0.0f, 0.0f}, // red
    {0.0f, 0.0f, 0.0f}, // black
    {1.0f, 1.0f, 0.0f}, // yellow
    {0.0f, 1.0f, 0.0f}, // green
    // right
    {1.0f, 0.0f, 1.0f}, // magenta
    {1.0f, 0.0f, 0.0f}, // red
    {1.0f, 1.0f, 1.0f}, // white
    {1.0f, 1.0f, 0.0f}, // yellow
    // left
    {0.0f, 0.0f, 0.0f}, // black
    {0.0f, 0.0f, 1.0f}, // blue
    {0.0f, 1.0f, 0.0f}, // green
    {0.0f, 1.0f, 1.0f}, // cyan
    // top
    {0.0f, 1.0f, 1.0f}, // cyan
    {1.0f, 1.0f, 1.0f}, // white
    {0.0f, 1.0f, 0.0f}, // green
    {1.0f, 1.0f, 0.0f}, // yellow
    // bottom
    {0.0f, 0.0f, 0.0f}, // black
    {1.0f, 0.0f, 0.0f}, // red
    {0.0f, 0.0f, 1.0f}, // blue
    {1.0f, 0.0f, 1.0f}  // magenta
};

static const float3 cube_normals[] = {
    // front
    {+0.0f, +0.0f, +1.0f}, // forward
    {+0.0f, +0.0f, +1.0f}, // forward
    {+0.0f, +0.0f, +1.0f}, // forward
    {+0.0f, +0.0f, +1.0f}, // forward
    // back
    {+0.0f, +0.0f, -1.0f}, // backbard
    {+0.0f, +0.0f, -1.0f}, // backbard
    {+0.0f, +0.0f, -1.0f}, // backbard
    {+0.0f, +0.0f, -1.0f}, // backbard
    // right
    {+1.0f, +0.0f, +0.0f}, // right
    {+1.0f, +0.0f, +0.0f}, // right
    {+1.0f, +0.0f, +0.0f}, // right
    {+1.0f, +0.0f, +0.0f}, // right
    // left
    {-1.0f, +0.0f, +0.0f}, // left
    {-1.0f, +0.0f, +0.0f}, // left
    {-1.0f, +0.0f, +0.0f}, // left
    {-1.0f, +0.0f, +0.0f}, // left
    // top
    {+0.0f, +1.0f, +0.0f}, // up
    {+0.0f, +1.0f, +0.0f}, // up
    {+0.0f, +1.0f, +0.0f}, // up
    {+0.0f, +1.0f, +0.0f}, // up
    // bottom
    {+0.0f, -1.0f, +0.0f}, // down
    {+0.0f, -1.0f, +0.0f}, // down
    {+0.0f, -1.0f, +0.0f}, // down
    {+0.0f, -1.0f, +0.0f}  // down
};

static const size_t cube_index_size = sizeof(cube_indices);
static const size_t cube_geom_size =
    sizeof(cube_positions) + sizeof(cube_colors) + sizeof(cube_normals);
static const size_t cube_index_count = sizeof(cube_indices) / sizeof(uint16_t);
static const size_t cube_vertex_count = sizeof(cube_positions) / sizeof(float3);

size_t cube_alloc_size() {
  return sizeof(CPUMesh) + cube_index_size + cube_geom_size;
}

void create_cube(CPUMesh *cube) {
  size_t offset = sizeof(CPUMesh);
  cube->indices = (uint16_t *)((uint8_t *)cube + offset);
  offset += cube_index_size;
  cube->vertices = ((uint8_t *)cube) + offset;

  uint8_t *pos = ((uint8_t *)cube) + offset;
  offset += sizeof(cube_positions);
  uint8_t *col = ((uint8_t *)cube) + offset;
  offset += sizeof(cube_colors);
  uint8_t *norm = ((uint8_t *)cube) + offset;

  cube->index_size = cube_index_size;
  cube->geom_size = cube_geom_size;
  cube->index_count = cube_index_count;
  cube->vertex_count = cube_vertex_count;

  memcpy((void *)cube->indices, cube_indices, cube_index_size);
  memcpy(pos, cube_positions, sizeof(cube_positions));
  memcpy(col, cube_colors, sizeof(cube_colors));
  memcpy(norm, cube_normals, sizeof(cube_normals));
}
