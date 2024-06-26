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
TracyCGPUContext *TracyCVkContext(VkPhysicalDevice gpu, VkDevice device,
                                  VkQueue queue, VkCommandBuffer buffer);
TracyCGPUContext *
TracyCVkContextExt(VkPhysicalDevice gpu, VkDevice device, VkQueue queue,
                   VkCommandBuffer buffer,
                   PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT ext1,
                   PFN_vkGetCalibratedTimestampsEXT ext2);
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
#define TracyCVkContextDestroy(...)
#define TracyCVkContextName(ctx, name, len) (void)ctx, (void)name, (void)len
#define TracyCVkNamedZone(ctx, var_name, cmd_buf, name, depth, active)         \
  int var_name = 0;                                                            \
  (void)ctx, (void)var_name, (void)cmd_buf, (void)name, (void)depth,           \
      (void)active
#define TracyCVkZoneEnd(...)
#define TracyCVkCollect(...)

#endif

#pragma clang diagnostic pop
