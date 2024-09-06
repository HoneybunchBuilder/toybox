#pragma once

#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++98-compat-pedantic"

#ifdef TRACY_ENABLE
#define TRACY_VK_C_ENABLE
#define TRACY_CALLSTACK 32
#endif
#include <tracy/TracyC.h>
#ifdef __cplusplus
#include <tracy/Tracy.hpp>
#endif

#define TracyCategoryColorCore 0xe066ff
#define TracyCategoryColorRendering 0x7fff00
#define TracyCategoryColorUI 0xe0eeee
#define TracyCategoryColorMath 0xffdab9
#define TracyCategoryColorInput 0xffb5c5
#define TracyCategoryColorMemory 0xff8c69
#define TracyCategoryColorWait 0xff0000
#define TracyCategoryColorGame 0x0022ff
#define TracyCategoryColorAudio 0xffff00
#define TracyCategoryColorPhysics 0x2ec4b6

#ifdef TRACY_VK_C_ENABLE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

typedef struct TracyCGPUContext TracyCGPUContext;
typedef struct TracyCGPUScope TracyCGPUScope;

#ifdef __cplusplus
extern "C" {
#endif

// A cleanup function used by TB_TRACY_SCOPE
void tb_tracy_zone_end(TracyCZoneCtx *ctx);

TracyCGPUContext *TracyCVkContext(VkPhysicalDevice gpu, VkDevice device,
                                  VkQueue queue, VkCommandBuffer buffer);
TracyCGPUContext *
TracyCVkContextExt(VkPhysicalDevice gpu, VkDevice device, VkQueue queue,
                   VkCommandBuffer buffer,
                   PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT ext1,
                   PFN_vkGetCalibratedTimestampsEXT ext2);
TracyCGPUContext *TracyCVkContextHostCalib(
    VkPhysicalDevice gpu, VkDevice device, PFN_vkResetQueryPoolEXT qpreset,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT gpdctd,
    PFN_vkGetCalibratedTimestampsEXT gct);
void TracyCVkContextDestroy(TracyCGPUContext *ctx);
void TracyCVkContextName(TracyCGPUContext *ctx, const char *name, size_t len);

TracyCGPUScope *
_TracyCVkNamedZone(TracyCGPUContext *ctx,
                   const struct ___tracy_source_location_data *source_loc,
                   VkCommandBuffer cmd_buf, const char *name, int depth,
                   bool active);
#define TracyCVkNamedZone(ctx, var_name, cmd_buf, name, depth, active)         \
  static const struct ___tracy_source_location_data TracyConcat(               \
      __tracy_source_location, __LINE__) = {NULL, __func__, __FILE__,          \
                                            (uint32_t)__LINE__, 0};            \
  TracyCGPUScope *var_name =                                                   \
      _TracyCVkNamedZone(ctx, &TracyConcat(__tracy_source_location, __LINE__), \
                         cmd_buf, name, depth, active);
void TracyCVkZoneEnd(TracyCGPUScope *scope);

void TracyCVkCollect(TracyCGPUContext *ctx, VkCommandBuffer cmd_buf);

#define TB_TRACY_SCOPE(name)                                                   \
  static const struct ___tracy_source_location_data TracyConcat(               \
      __tracy_source_location, TracyLine) = {name, __func__, TracyFile,        \
                                             (uint32_t)TracyLine, 0};          \
  __attribute__((cleanup(tb_tracy_zone_end))) TracyCZoneCtx ctx##__COUNTER__ = \
      ___tracy_emit_zone_begin_callstack(                                      \
          &TracyConcat(__tracy_source_location, TracyLine), TRACY_CALLSTACK,   \
          true);

#define TB_TRACY_SCOPEC(name, color)                                           \
  static const struct ___tracy_source_location_data TracyConcat(               \
      __tracy_source_location, TracyLine) = {name, __func__, TracyFile,        \
                                             (uint32_t)TracyLine, color};      \
  __attribute__((cleanup(tb_tracy_zone_end))) TracyCZoneCtx ctx##__COUNTER__ = \
      ___tracy_emit_zone_begin_callstack(                                      \
          &TracyConcat(__tracy_source_location, TracyLine), TRACY_CALLSTACK,   \
          true);

#ifdef __cplusplus
}
#endif
#else

typedef struct TracyCGPUContext TracyCGPUContext;
typedef struct TracyCGPUScope TracyCGPUScope;

#define TracyCVkContextExt(gpu, device, queue, buffer, ext1, ext2)             \
  0;                                                                           \
  (void)gpu, (void)device, (void)queue, (void)buffer, (void)ext1, (void)ext2;

#define TracyCVkContext(...)
#define TracyCVkContextHostCalib(gpu, device, fn1, fn2, fn3)                   \
  0;                                                                           \
  (void)gpu, (void)device, (void)fn1, (void)fn2, (void)fn3
#define TracyCVkContextDestroy(...)
#define TracyCVkContextName(ctx, name, len) (void)ctx, (void)name, (void)len
#define TracyCVkNamedZone(ctx, var_name, cmd_buf, name, depth, active)         \
  int var_name = 0;                                                            \
  (void)ctx, (void)var_name, (void)cmd_buf, (void)name, (void)depth,           \
      (void)active
#define TracyCVkZoneEnd(...)
#define TracyCVkCollect(...)

#define TB_TRACY_SCOPE(...)
#define TB_TRACY_SCOPEC(...)

#endif

#pragma clang diagnostic pop
