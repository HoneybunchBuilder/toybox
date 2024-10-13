
#include <phonon.h>

#include "tb_audio_system.h"
#include "tb_common.h"
#include "tb_log.h"
#include "tb_profiling.h"
#include "tb_system_priority.h"
#include "tb_world.h"

typedef struct TbSpatialAudioSystem {
  IPLContext ctx;
  IPLHRTF hrtf;
  IPLAudioSettings audio_settings;
} TbSpatialAudioSystem;

ECS_COMPONENT_DECLARE(TbSpatialAudioSystem);

void *tb_spatial_audio_alloc(size_t size, size_t alignment) {
  void *ptr = tb_alloc_aligned(tb_global_alloc, size, alignment);
  TracyCAllocN(ptr, size, "Steam Audio");
  return ptr;
}

void tb_spatial_audio_free(void *ptr) {
  TracyCFreeN(ptr, "Steam Audio");
  tb_free(tb_global_alloc, ptr);
}

void tb_spatial_audio_log(IPLLogLevel level, const char *message) {
  (void)message;
  switch (level) {
  case IPL_LOGLEVEL_DEBUG:
    TB_LOG_DEBUG(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  case IPL_LOGLEVEL_WARNING:
    TB_LOG_WARN(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  case IPL_LOGLEVEL_ERROR:
    TB_LOG_ERROR(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  case IPL_LOGLEVEL_INFO:
  default:
    TB_LOG_INFO(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  }
}

void tb_register_spatial_audio_sys(TbWorld *world) {
  TB_TRACY_SCOPEC("Register Spatial Audio Sys", TracyCategoryColorAudio);

  ECS_COMPONENT_DEFINE(world->ecs, TbSpatialAudioSystem);

  TbSpatialAudioSystem sys = {0};

  IPLContextSettings ctx_settings = {
      .version = STEAMAUDIO_VERSION,
      .logCallback = tb_spatial_audio_log,
      .allocateCallback = tb_spatial_audio_alloc,
      .freeCallback = tb_spatial_audio_free,
      .simdLevel = IPL_SIMDLEVEL_SSE4,
      .flags = (IPLContextFlags)0, // Could optionally request validation
  };
  tb_auto error = iplContextCreate(&ctx_settings, &sys.ctx);
  (void)error;
  TB_CHECK(error == IPL_STATUS_SUCCESS, "Failed to create spatial audio ctx");

  tb_auto audio_sys = ecs_singleton_get(world->ecs, TbAudioSystem);

  // Load Default HRTF
  sys.audio_settings = (IPLAudioSettings){
      .samplingRate = audio_sys->frequency,
      .frameSize = 1024,
  };
  IPLHRTFSettings hrtf_settings = {
      .type = IPL_HRTFTYPE_DEFAULT,
  };
  iplHRTFCreate(sys.ctx, &sys.audio_settings, &hrtf_settings, &sys.hrtf);

  /*
    TODO:
    When a spatial audio source component is created, create a binaural audio
    effect.
    When the audio source plays it will tell a channel to play the chunk
    Mix_RegisterEffect will be used to supply a custom effect processing
    routine.
    That routine will take the binaural audio effect and its parameters
    and on the fly apply the effect in the channel effect routine.
  */

  ecs_singleton_set_ptr(world->ecs, TbSpatialAudioSystem, &sys);
}

void tb_unregister_spatial_audio_sys(TbWorld *world) {
  tb_auto sys = ecs_singleton_ensure(world->ecs, TbSpatialAudioSystem);
  iplHRTFRelease(&sys->hrtf);
  iplContextRelease(&sys->ctx);
  *sys = (TbSpatialAudioSystem){0};
  ecs_singleton_remove(world->ecs, TbSpatialAudioSystem);
}

TB_REGISTER_SYS(tb, spatial_audio, (TB_AUDIO_SYS_PRIO + 1))
