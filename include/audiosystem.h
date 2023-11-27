#pragma once

#include "allocator.h"
#include "dynarray.h"

#define TB_AUDIO_CHUNK_SIZE 2048

typedef struct TbWorld TbWorld;

typedef uint32_t TbMusicId;
typedef uint32_t TbSoundEffectId;

typedef struct TbMusic TbMusic;
typedef struct TbSoundEffect TbSoundEffect;

typedef struct TbAudioSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  int32_t frequency;
  uint16_t format;
  int32_t channels;

  TB_DYN_ARR_OF(TbMusic) music;
  TB_DYN_ARR_OF(TbSoundEffect) sfx;
} TbAudioSystem;

void tb_register_audio_sys(TbWorld *world);
void tb_unregister_audio_sys(TbWorld *world);

TbMusicId tb_audio_system_load_music(TbAudioSystem *self, const char *path);
TbSoundEffectId tb_audio_system_load_effect(TbAudioSystem *self,
                                            const char *path);
void tb_audio_system_release_music_ref(TbAudioSystem *self, TbMusicId id);
void tb_audio_system_release_effect_ref(TbAudioSystem *self,
                                        TbSoundEffectId id);

void tb_audio_play_music(TbAudioSystem *self, TbMusicId id);
void tb_audio_play_effect(TbAudioSystem *self, TbSoundEffectId id);
