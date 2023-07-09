#include "audiosystem.h"

#include "tbcommon.h"
#include "world.h"

#include <SDL2/SDL_mixer.h>

typedef struct TbMusic {
  uint32_t ref_count;
  Mix_Music *music;
} TbMusic;

bool create_audio_system(AudioSystem *self, const AudioSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  int32_t ret = Mix_Init(MIX_INIT_OGG);
  TB_CHECK_RETURN(ret != 0, "Failed to initialize SDL2 Mixer", false);

  // Default to 44khz 16-bit stereo for now
  // But allow the mixer to change these if the device wants something specific
  ret = Mix_OpenAudioDevice(44100, AUDIO_S16SYS, 2, TB_AUDIO_CHUNK_SIZE, NULL,
                            SDL_AUDIO_ALLOW_ANY_CHANGE);
  TB_CHECK_RETURN(ret == 0, "Failed to open default audio device", false);

  int32_t freq = 0;
  uint16_t format = 0;
  int32_t channels = 0;
  ret = Mix_QuerySpec(&freq, &format, &channels);
  TB_CHECK_RETURN(ret == 1, "Failed to query audio device", false);

  // Set the number of audio tracks to 8 for starters
  ret = Mix_AllocateChannels(8);
  TB_CHECK_RETURN(ret != 0, "Failed to allocate tracks for audio device",
                  false);

  *self = (AudioSystem){
      .std_alloc = desc->std_alloc,
      .tmp_alloc = desc->tmp_alloc,
      .frequency = freq,
      .format = format,
      .channels = channels,
  };
  TB_DYN_ARR_RESET(self->music, self->std_alloc, 8);

  return true;
}

void destroy_audio_system(AudioSystem *self) {
  TB_DYN_ARR_FOREACH(self->music, i) {
    TB_CHECK(TB_DYN_ARR_AT(self->music, i).ref_count == 0, "Leaking music");
  }

  TB_DYN_ARR_DESTROY(self->music);
  Mix_CloseAudio();
  Mix_Quit();
  *self = (AudioSystem){0};
}

void tick_audio_system(AudioSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(audio, AudioSystem, AudioSystemDescriptor)

void tb_audio_system_descriptor(SystemDescriptor *desc,
                                const AudioSystemDescriptor *audio_desc) {
  *desc = (SystemDescriptor){
      .name = "Audio",
      .size = sizeof(AudioSystem),
      .id = AudioSystemId,
      .desc = (InternalDescriptor)audio_desc,
      .create = tb_create_audio_system,
      .destroy = tb_destroy_audio_system,
      .tick = tb_tick_audio_system,
  };
}

TbMusicId tb_audio_system_load_music(AudioSystem *self, const char *path) {
  Mix_Music *music = Mix_LoadMUS(path);
  TB_CHECK_RETURN(music, "Failed to load music", 0xFFFFFFFF);
  TbMusicId id = (TbMusicId)TB_DYN_ARR_SIZE(self->music);
  TbMusic m = {1, music}; // Assume that by loading the music we take a ref
  TB_DYN_ARR_APPEND(self->music, m);
  return id;
}

void tb_audio_system_release_music_ref(AudioSystem *self, TbMusicId id) {
  TbMusic *music = &TB_DYN_ARR_AT(self->music, id);
  TB_CHECK(
      music->ref_count > 0,
      "Trying to release reference to music that has no reference holders");
  music->ref_count--;
  if (music->ref_count == 0) {
    Mix_FreeMusic(music->music);
  }
}

void tb_audio_play_music(AudioSystem *self, TbMusicId id) {
  TbMusic *music = &TB_DYN_ARR_AT(self->music, id);
  Mix_PlayMusic(music->music, 1);
}
