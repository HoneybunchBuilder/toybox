#include "profiling.h"

#ifdef TRACY_ENABLE

#include "Tracy.hpp"
#include "client/TracyCallstack.hpp"
#include "client/TracyProfiler.hpp"
#include <assert.h>
#include <stdlib.h>

#include <volk.h>

#include "TracyVulkan.hpp"

extern "C" {

TracyCGPUContext *TracyCVkContext(VkPhysicalDevice gpu, VkDevice device,
                                  VkQueue queue, VkCommandBuffer buffer) {
  auto ctx = (tracy::VkCtx *)tracy::tracy_malloc(sizeof(tracy::VkCtx));
  new (ctx) tracy::VkCtx(gpu, device, queue, buffer, nullptr, nullptr);
  return (TracyCGPUContext *)ctx;
}

TracyCGPUContext *
TracyCVkContextExt(VkPhysicalDevice gpu, VkDevice device, VkQueue queue,
                   VkCommandBuffer buffer,
                   PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT ext1,
                   PFN_vkGetCalibratedTimestampsEXT ext2) {
  auto ctx = (tracy::VkCtx *)tracy::tracy_malloc(sizeof(tracy::VkCtx));
  new (ctx) tracy::VkCtx(gpu, device, queue, buffer, ext1, ext2);
  return (TracyCGPUContext *)ctx;
}

void TracyCVkContextDestroy(TracyCGPUContext *ctx) {
  auto *c = (tracy::VkCtx *)ctx;
  c->~VkCtx();
  tracy::tracy_free((void *)c);
}

void TracyCVkContextName(TracyCGPUContext *ctx, const char *name, size_t len) {
  ((tracy::VkCtx *)ctx)->Name(name, (uint16_t)len);
}

TracyCGPUScope *
_TracyCVkNamedZone(TracyCGPUContext *ctx,
                   const struct ___tracy_source_location_data *source_loc,
                   VkCommandBuffer cmd_buf, const char *name, int depth,
                   bool active) {
  auto scope =
      (tracy::VkCtxScope *)tracy::tracy_malloc(sizeof(tracy::VkCtxScope));
  new (scope) tracy::VkCtxScope(
      (tracy::VkCtx *)ctx, source_loc->line, source_loc->file,
      strlen(source_loc->file), source_loc->function,
      strlen(source_loc->function), name, strlen(name), cmd_buf, depth, active);
  return (TracyCGPUScope *)scope;
}
void TracyCVkZoneEnd(TracyCGPUScope *scope) {
  tracy::VkCtxScope *s = (tracy::VkCtxScope *)scope;
  s->~VkCtxScope();
  tracy::tracy_free(s);
}

void TracyCVkCollect(TracyCGPUContext *ctx, VkCommandBuffer cmd_buf) {
  tracy::VkCtx *tracy_ctx = (tracy::VkCtx *)ctx;
  tracy_ctx->Collect(cmd_buf);
}
}

#endif