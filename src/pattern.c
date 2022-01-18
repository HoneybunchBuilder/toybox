#include "pattern.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "cpuresources.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

void write_pattern(uint32_t width, uint32_t height, uint32_t *bitmap) {
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      int l = (0x1FF >>
               MIN(MIN(MIN(MIN(x, y), width - 1 - x), height - 1 - y), 31u));
      int d =
          MIN(50, MAX(0, 255 - 50 * powf(hypotf(x / (float)(width / 2) - 1.f,
                                                y / (float)(height / 2) - 1.f) *
                                             4,
                                         2.f)));
      int r = (~x & ~y) & 255, g = (x & ~y) & 255, b = (~x & y) & 255;
      bitmap[y * width + x] = MIN(MAX(r - d, l), 255) * 65536 +
                              MIN(MAX(g - d, l), 255) * 256 +
                              MIN(MAX(b - d, l), 255);
    }
  }
}

void alloc_pattern(Allocator alloc, uint32_t width, uint32_t height,
                   CPUTexture **out) {
  assert(out);
  uint64_t data_size = width * height * sizeof(uint32_t);

  CPUTexture *tex =
      hb_alloc(alloc, data_size + sizeof(CPUTexture) + sizeof(TextureLayer) +
                          sizeof(TextureMip));
  assert(tex);

  uint64_t offset = sizeof(CPUTexture);

  tex->layer_count = 1;
  tex->mip_count = 1;
  tex->layers = (const TextureLayer *)(((uint8_t *)tex) + offset);
  offset += sizeof(TextureLayer) * tex->layer_count;

  TextureLayer *layer = (TextureLayer *)&tex->layers[0];
  layer->mips = (const TextureMip *)(((uint8_t *)tex) + offset);
  offset += sizeof(TextureMip) * tex->mip_count;

  tex->data_size = data_size;
  tex->data = (((uint8_t *)tex) + offset);

  (*out) = tex;
}

void create_pattern(uint32_t width, uint32_t height, CPUTexture *out) {
  assert(out);

  // Setup subresource
  TextureLayer *layer = (TextureLayer *)&out->layers[0];
  layer->width = width;
  layer->height = height;
  layer->depth = 1;

  TextureMip *mip = (TextureMip *)&layer->mips[0];
  mip->width = width;
  mip->height = height;
  mip->depth = 1;
  mip->data = out->data;

  // Create bitmap
  write_pattern(width, height, (uint32_t *)out->data);
}
