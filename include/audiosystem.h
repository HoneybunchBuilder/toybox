#pragma once

#include "allocator.h"
#include "dynarray.h"

#define AudioSystemId 0xCAFED00D

#define TB_AUDIO_CHUNK_SIZE 2048

typedef struct SystemDescriptor SystemDescriptor;

typedef uint32_t TbMusicId;

typedef struct TbMusic TbMusic;

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

  TB_DYN_ARR_OF(TbMusic) music;
} AudioSystem;

void tb_audio_system_descriptor(SystemDescriptor *desc,
                                const AudioSystemDescriptor *audio_desc);

TbMusicId tb_audio_system_load_music(AudioSystem *self, const char *path);
void tb_audio_system_release_music_ref(AudioSystem *self, TbMusicId id);

void tb_audio_play_music(AudioSystem *self, TbMusicId id);
