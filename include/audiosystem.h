#pragma once

#include "allocator.h"

#define AudioSystemId 0xCAFED00D

#define TB_AUDIO_CHUNK_SIZE 2048

typedef struct SystemDescriptor SystemDescriptor;

typedef struct AudioSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} AudioSystemDescriptor;

typedef struct AudioSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  int32_t frequency;
  uint16_t format;
  int32_t channels;
} AudioSystem;

void tb_audio_system_descriptor(SystemDescriptor *desc,
                                const AudioSystemDescriptor *audio_desc);
