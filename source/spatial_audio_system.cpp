
#include <phonon.h>

#include "profiling.h"
#include "tbcommon.h"
#include "tblog.h"
#include "tbsystempriority.h"
#include "world.h"

struct TbSpatialAudioSystem {
  IPLContext ipl_ctx;
};

void *tb_spatial_audio_alloc(size_t size, size_t alignment) {
  void *ptr = tb_alloc_aligned(tb_global_alloc, size, alignment);
  TracyCAllocN(ptr, size, "Steam Audio");
  return ptr;
};

void tb_spatial_audio_free(void *ptr) {
  TracyCFreeN(ptr, "Steam Audio");
  tb_free(tb_global_alloc, ptr);
}

void tb_spatial_audio_log(IPLLogLevel level, const char *message) {
  switch (level) {
  case IPLLogLevel::IPL_LOGLEVEL_DEBUG:
    TB_LOG_DEBUG(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  case IPLLogLevel::IPL_LOGLEVEL_WARNING:
    TB_LOG_WARN(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  case IPLLogLevel::IPL_LOGLEVEL_ERROR:
    TB_LOG_ERROR(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  case IPLLogLevel::IPL_LOGLEVEL_INFO:
  default:
    TB_LOG_INFO(TB_LOG_CATEGORY_SPATIAL_AUDIO, "%s", message);
    break;
  }
}

void tb_register_spatial_audio_sys(TbWorld *world) {
  ZoneScopedN("Register Spatial Audio Sys");
  flecs::world ecs(world->ecs);

  TbSpatialAudioSystem sys = {};

  IPLContextSettings ctx_settings = {
      .version = STEAMAUDIO_VERSION,
      .logCallback = tb_spatial_audio_log,
      .allocateCallback = tb_spatial_audio_alloc,
      .freeCallback = tb_spatial_audio_free,
      .simdLevel = IPLSIMDLevel::IPL_SIMDLEVEL_SSE4,
      .flags = (IPLContextFlags)0, // Could optionally request validation
  };
  auto error = iplContextCreate(&ctx_settings, &sys.ipl_ctx);
  (void)error;
  TB_CHECK(error == IPL_STATUS_SUCCESS, "Failed to create spatial audio ctx");

  ecs.set<TbSpatialAudioSystem>(sys);
}

void tb_unregister_spatial_audio_sys(TbWorld *world) {
  flecs::world ecs(world->ecs);
  auto sys = ecs.get_ref<TbSpatialAudioSystem>().get();
  iplContextRelease(&sys->ipl_ctx);
  *sys = {};
  ecs.remove<TbSpatialAudioSystem>();
}

extern "C" {
// Helper macro to auto-register system
TB_REGISTER_SYS(tb, spatial_audio, TB_SYSTEM_HIGH);
}
