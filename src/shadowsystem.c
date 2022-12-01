#include "shadowsystem.h"

#include "lightcomponent.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "world.h"

bool create_shadow_system(ShadowSystem *self,
                          const ShadowSystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  // Find necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which shadows depend on",
                  false);
  RenderTargetSystem *render_target_system =
      (RenderTargetSystem *)tb_find_system_dep_self_by_id(
          system_deps, system_dep_count, RenderTargetSystemId);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find render target system which shadows depend on",
                  false);

  *self = (ShadowSystem){
      .render_system = render_system,
      .render_target_system = render_target_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };
  return true;
}

void destroy_shadow_system(ShadowSystem *self) { *self = (ShadowSystem){0}; }

void tick_shadow_system(ShadowSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(shadow, ShadowSystem, ShadowSystemDescriptor)

void tb_shadow_system_descriptor(SystemDescriptor *desc,
                                 const ShadowSystemDescriptor *shadow_desc) {
  *desc = (SystemDescriptor){
      .name = "Shadow",
      .size = sizeof(ShadowSystem),
      .id = ShadowSystemId,
      .desc = (InternalDescriptor)shadow_desc,
      .dep_count = 1,
      .deps[0] =
          {
              .count = 1,
              .dependent_ids = {DirectionalLightComponentId},
          },
      .system_dep_count = 2,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = RenderTargetSystemId,
      .create = tb_create_shadow_system,
      .destroy = tb_destroy_shadow_system,
      .tick = tb_tick_shadow_system,
  };
}
