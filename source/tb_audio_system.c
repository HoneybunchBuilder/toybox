#include "tb_audio_system.h"

#include "tb_common.h"
#include "tb_profiling.h"
#include "tb_world.h"

#include <SDL3_mixer/SDL_mixer.h>

ECS_COMPONENT_DECLARE(TbAudioSystem);

typedef struct TbMusic {
  uint32_t ref_count;
  Mix_Music *music;
} TbMusic;

typedef struct TbSoundEffect {
  uint32_t ref_count;
  Mix_Chunk *chunk;
} TbSoundEffect;

void tb_register_audio_sys(TbWorld *world);
void tb_unregister_audio_sys(TbWorld *world);

TB_REGISTER_SYS(tb, audio, TB_AUDIO_SYS_PRIO)

void destroy_audio_system(TbAudioSystem *self) {
  TB_DYN_ARR_FOREACH(self->music, i) {
    TB_CHECK(TB_DYN_ARR_AT(self->music, i).ref_count == 0, "Leaking music");
  }
  TB_DYN_ARR_FOREACH(self->sfx, i) {
    TB_CHECK(TB_DYN_ARR_AT(self->sfx, i).ref_count == 0, "Leaking effects");
  }

  TB_DYN_ARR_DESTROY(self->music);
  TB_DYN_ARR_DESTROY(self->sfx);

  Mix_CloseAudio();
  Mix_Quit();
  *self = (TbAudioSystem){0};
}

void tb_register_audio_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Audio Sys");
  tb_auto ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbAudioSystem);

  int32_t ret = Mix_Init(MIX_INIT_OGG);
  TB_CHECK(ret != 0, "Failed to initialize SDL3 Mixer");

  // Open the first default device
  SDL_AudioSpec audio_spec = {
      .channels = MIX_DEFAULT_CHANNELS,
      .format = MIX_DEFAULT_FORMAT,
      .freq = MIX_DEFAULT_FREQUENCY,
  };

  bool ok = Mix_OpenAudio(0, &audio_spec);
  TB_CHECK(ok, "Failed to open default audio device");

  ok =
      Mix_QuerySpec(&audio_spec.freq, &audio_spec.format, &audio_spec.channels);
  TB_CHECK(ok, "Failed to query audio device");

  // Set the number of audio tracks to 8 for starters
  int32_t channels = Mix_AllocateChannels(8);
  TB_CHECK(channels != 0, "Failed to allocate tracks for audio device");

  TbAudioSystem sys = {
      .gp_alloc = world->gp_alloc,
      .tmp_alloc = world->tmp_alloc,
      .frequency = audio_spec.freq,
      .format = audio_spec.format,
      .channels = audio_spec.channels,
  };
  TB_DYN_ARR_RESET(sys.music, sys.gp_alloc, 8);
  TB_DYN_ARR_RESET(sys.sfx, sys.gp_alloc, 8);

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbAudioSystem), TbAudioSystem, &sys);
}

void tb_unregister_audio_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  tb_auto sys = ecs_singleton_get_mut(ecs, TbAudioSystem);
  destroy_audio_system(sys);
}

TbMusicId tb_audio_system_load_music(TbAudioSystem *self, const char *path) {
  TracyCZoneN(ctx, "Audio System Load Music", true);
  TracyCZoneColor(ctx, TracyCategoryColorAudio);

  Mix_Music *music = Mix_LoadMUS(path);
  TB_CHECK_RETURN(music, "Failed to load music", 0xFFFFFFFF);
  TbMusicId id = (TbMusicId)TB_DYN_ARR_SIZE(self->music);
  TbMusic m = {1, music}; // Assume that by loading the music we take a ref
  TB_DYN_ARR_APPEND(self->music, m);

  TracyCZoneEnd(ctx);
  return id;
}

TbSoundEffectId tb_audio_system_load_effect(TbAudioSystem *self,
                                            const char *path) {
  TracyCZoneN(ctx, "Audio System Load Effect", true);
  TracyCZoneColor(ctx, TracyCategoryColorAudio);

  Mix_Chunk *chunk = Mix_LoadWAV(path);
  TB_CHECK_RETURN(chunk, "Failed to load effect", 0xFFFFFFFF);
  TbSoundEffectId id = (TbSoundEffectId)TB_DYN_ARR_SIZE(self->sfx);
  TbSoundEffect c = {1,
                     chunk}; // Assume that by loading the chunk we take a ref
  TB_DYN_ARR_APPEND(self->sfx, c);

  TracyCZoneEnd(ctx);
  return id;
}

void tb_audio_system_release_music_ref(TbAudioSystem *self, TbMusicId id) {
  TbMusic *music = &TB_DYN_ARR_AT(self->music, id);
  TB_CHECK(
      music->ref_count > 0,
      "Trying to release reference to music that has no reference holders");
  music->ref_count--;
  if (music->ref_count == 0) {
    Mix_FreeMusic(music->music);
  }
}

void tb_audio_system_release_effect_ref(TbAudioSystem *self,
                                        TbSoundEffectId id) {
  TbSoundEffect *effect = &TB_DYN_ARR_AT(self->sfx, id);
  TB_CHECK(
      effect->ref_count > 0,
      "Trying to release reference to effect that has no reference holders");
  effect->ref_count--;
  if (effect->ref_count == 0) {
    Mix_FreeChunk(effect->chunk);
  }
}

void tb_audio_play_music(TbAudioSystem *self, TbMusicId id) {
  TracyCZoneN(ctx, "Audio System Play Music", true);
  TracyCZoneColor(ctx, TracyCategoryColorAudio);

  TbMusic *music = &TB_DYN_ARR_AT(self->music, id);
  TB_CHECK(music->ref_count > 0,
           "Trying to play music that has no reference holders");
  Mix_PlayMusic(music->music, SDL_MAX_SINT32);

  TracyCZoneEnd(ctx);
}

void tb_audio_play_effect(TbAudioSystem *self, TbSoundEffectId id) {
  TracyCZoneN(ctx, "Audio System Play Effect", true);
  TracyCZoneColor(ctx, TracyCategoryColorAudio);

  TbSoundEffect *effect = &TB_DYN_ARR_AT(self->sfx, id);
  TB_CHECK(effect->ref_count > 0,
           "Trying to play effect that has no reference holders");

  Mix_PlayChannel(-1, effect->chunk, 0);

  TracyCZoneEnd(ctx);
}
