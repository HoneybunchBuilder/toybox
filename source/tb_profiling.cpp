#include "tb_profiling.h"

#ifdef TRACY_VK_C_ENABLE

#include "client/TracyCallstack.hpp"
#include "client/TracyProfiler.hpp"
#include "tracy/Tracy.hpp"
#include <assert.h>
#include <stdlib.h>

#include "tb_vk.h"

#include "tracy/TracyVulkan.hpp"

extern "C" {

TracyCGPUContext *TracyCVkContext(VkPhysicalDevice gpu, VkDevice device,
                                  VkQueue queue, VkCommandBuffer buffer) {
  auto ctx =
      tracy::CreateVkContext(gpu, device, queue, buffer, nullptr, nullptr);
  return (TracyCGPUContext *)ctx;
}

TracyCGPUContext *
TracyCVkContextExt(VkPhysicalDevice gpu, VkDevice device, VkQueue queue,
                   VkCommandBuffer buffer,
                   PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT ext1,
                   PFN_vkGetCalibratedTimestampsEXT ext2) {
  auto ctx = tracy::CreateVkContext(gpu, device, queue, buffer, ext1, ext2);
  return (TracyCGPUContext *)ctx;
}

TracyCGPUContext *TracyCVkContextHostCalib(
    VkPhysicalDevice gpu, VkDevice device, PFN_vkResetQueryPoolEXT qpreset,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT gpdctd,
    PFN_vkGetCalibratedTimestampsEXT gct) {
  auto ctx = tracy::CreateVkContext(gpu, device, qpreset, gpdctd, gct);
  return (TracyCGPUContext *)ctx;
}

void TracyCVkContextDestroy(TracyCGPUContext *ctx) {
  tracy::DestroyVkContext((tracy::VkCtx *)ctx);
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
  new (scope)
      tracy::VkCtxScope((tracy::VkCtx *)ctx, source_loc->line, source_loc->file,
                        strlen(source_loc->file), source_loc->function,
                        strlen(source_loc->function), name, strlen(name) + 1,
                        cmd_buf, depth, active);
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

#else

void ___tracy_set_thread_name(const char *name) { (void)name; }

#endif
