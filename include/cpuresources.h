#pragma once

#include "simd.h"

/*
  Struct describing a mesh where each vertex element is packed in-line.
  eg: [x, y, z, u, v, r, g, b, x, y, z, u,, v, r, g, b, ...]

  User must take caution to ensure that the contents of this buffer
  line up with the layout it is used in conjunction with. It does
  not contain info about the stride of each attribute of each vertex.
*/
typedef struct CPUMesh {
  uint64_t index_size;     // size of the indices buffer in bytes
  uint64_t geom_size;      // size of the vertices buffer in bytes
  uint32_t index_count;    // Number of indices
  uint32_t vertex_count;   // Number of vertices
  const uint16_t *indices; // index buffer
  const uint8_t *vertices; // vertex buffer
} CPUMesh;

#define MAX_CPUMESH_ELEMENTS 8
/*
  Struct describing a mesh where each vertex element has a unique region in the
  buffer.
  eg: [x, y, z, x, y, z, ..., u, v, u, v, ... , r, g, b, r, g, b, ...]

  User must take caution to ensure that the contents of this buffer
  line up with the layout it is used in conjunction with.
*/
typedef struct CPUMeshBuffers {
  uint64_t index_size;                    // size of the index buffer in bytes
  uint64_t geom_size;                     // size of the vertex buffer in bytes
  uint32_t index_count;                   // number of indices
  uint32_t elements;                      // number of elements per vertex
  uint32_t offsets[MAX_CPUMESH_ELEMENTS]; // starting offset of each vertex
                                          // element
  uint32_t sizes[MAX_CPUMESH_ELEMENTS];   // size of each vertex element
  const uint16_t *indices;                // index buffer
  const uint8_t *vertices;                // buffer containing all vertex data.
} CPUMeshBuffers;

typedef struct TextureMip {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  const uint8_t *data;
} TextureMip;

typedef struct TextureLayer {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  const TextureMip *mips;
} TextureLayer;

typedef struct CPUTexture {
  uint32_t layer_count;
  uint32_t mip_count;
  const TextureLayer *layers;
  uint64_t data_size;
  const uint8_t *data;
} CPUTexture;
