#pragma once

#include <stdbool.h>

#define TRACY_ENABLE
#include <TracyC.h>

#define TracyCategoryColorCore 0xe066ff
#define TracyCategoryColorRendering 0x7fff00
#define TracyCategoryColorUI 0xe0eeee
#define TracyCategoryColorMath 0xffdab9
#define TracyCategoryColorInput 0xffb5c5
#define TracyCategoryColorMemory 0xff8c69
#define TracyCategoryColorWait 0xff0000

#ifdef TRACY_ENABLE
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

#define TracyCGPUContext int
#define TracyCGPUScope int

#define TracyCVkContextExt(...) 0
#define TracyCVkContext(...)
#define TracyCVkContextDestroy(...)
#define TracyCVkContextName(...)
#define TracyCVkNamedZone(...)
#define TracyCVkZoneEnd(...)
#define TracyCVkCollect(...)

#endif
