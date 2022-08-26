#include "rendersystem.h"

#include "profiling.h"
#include "tbsdl.h"
#include "transformcomponent.h"
#include "world.h"

bool create_render_system(RenderSystem *self,
                          const RenderSystemDescriptor *desc) {
  if (!desc) {
    return false;
  }

  *self = (RenderSystem){
      .window = desc->window,
      .instance = desc->instance,
      .std_alloc = desc->std_alloc,
      .tmp_alloc = desc->tmp_alloc,
      .vk_alloc = desc->vk_alloc,
  };
  return true;
}

void destroy_render_system(RenderSystem *self) { *self = (RenderSystem){0}; }

void tick_render_system(RenderSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output; // Won't actually have output to the world but will write to the
                // screen we hope
  (void)delta_seconds;

  TracyCZoneN(tick_ctx, "Render System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorRendering);

  const PackedComponentStore *transform_store = NULL;

  // TODO: Properly use multiple dependency sets
  for (uint32_t dep_idx = 0; dep_idx < input->dep_set_count; ++dep_idx) {
    const SystemDependencySet *set = &input->dep_sets[dep_idx];
    for (uint32_t col_idx = 0; col_idx < set->column_count; ++col_idx) {
      const PackedComponentStore *store = set->columns[col_idx];
      if (store->id == TransformComponentId) {
        transform_store = store;
      }
    }
  }

  if (transform_store == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s",
                 "Failed to retrieve transform components for render system.");
    SDL_TriggerBreakpoint();
    return;
  }

  const TransformComponent *transform_components =
      (const TransformComponent *)transform_store->components;
  const uint32_t transform_count = transform_store->count;

  for (uint32_t i = 0; i < transform_count; ++i) {
    const Transform *t = &transform_components[i].transform;
    (void)t;
  }

  SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Ticking Render System");

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(render, RenderSystem, RenderSystemDescriptor)

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc) {
  desc->name = "Render";
  desc->size = sizeof(RenderSystem);
  desc->id = RenderSystemId;
  desc->desc = (InternalDescriptor)render_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){1,
                                                {
                                                    TransformComponentId,
                                                }};
  desc->create = tb_create_render_system;
  desc->destroy = tb_destroy_render_system;
  desc->tick = tb_tick_render_system;
}
