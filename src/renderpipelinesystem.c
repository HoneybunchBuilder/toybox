#include "renderpipelinesystem.h"

#include "rendersystem.h"
#include "texturesystem.h"
#include "world.h"

bool create_render_pipeline_system(RenderPipelineSystem *self,
                                   const RenderPipelineSystemDescriptor *desc,
                                   uint32_t system_dep_count,
                                   System *const *system_deps) {
  // Find necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(
      render_system,
      "Failed to find render system which the render pipeline depends on",
      false);
  TextureSystem *texture_system =
      (TextureSystem *)tb_find_system_dep_self_by_id(
          system_deps, system_dep_count, TextureSystemId);
  TB_CHECK_RETURN(
      texture_system,
      "Failed to find texture system which the render pipeline depends on",
      false);

  *self = (RenderPipelineSystem){
      .render_system = render_system,
      .texture_system = texture_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Create some default passes

  return true;
}

void destroy_render_pipeline_system(RenderPipelineSystem *self) {
  *self = (RenderPipelineSystem){0};
}

void tick_render_pipeline_system(RenderPipelineSystem *self,
                                 const SystemInput *input, SystemOutput *output,
                                 float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(render_pipeline, RenderPipelineSystem,
                 RenderPipelineSystemDescriptor)

void tb_render_pipeline_system_descriptor(
    SystemDescriptor *desc, const RenderPipelineSystemDescriptor *pipe_desc) {
  *desc = (SystemDescriptor){
      .name = "Render Pipeline",
      .size = sizeof(RenderPipelineSystem),
      .id = RenderPipelineSystemId,
      .desc = (InternalDescriptor)pipe_desc,
      .dep_count = 0,
      .system_dep_count = 2,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = TextureSystemId,
      .create = tb_create_render_pipeline_system,
      .destroy = tb_destroy_render_pipeline_system,
      .tick = tb_tick_render_pipeline_system,
  };
}
