#include "imguicomponent.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "tbimgui.h"
#include "world.h"

bool create_imgui_component(ImGuiComponent *self,
                            const ImGuiComponentDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  // Ensure we have a reference to the render system
  RenderSystem *render_system = NULL;
  for (uint32_t i = 0; i < system_dep_count; ++i) {
    System *sys = system_deps[i];
    if (sys->id == RenderSystemId) {
      render_system = (RenderSystem *)sys;
      break;
    }
  }
  TB_CHECK_RETURN(render_system, "Failed to get render system reference",
                  false);

  *self = (ImGuiComponent){
      .context = igCreateContext(desc->font_atlas),
  };

  // Get atlas texture data for this context
  ImGuiIO *io = igGetIO();

  uint8_t *pixels = NULL;
  int32_t tex_w = 0;
  int32_t tex_h = 0;
  int32_t bytes_pp = 0;
  ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &tex_w, &tex_h, &bytes_pp);

  // Copy this texture to host visible image
  {}

  // Setup basic display size
  io->DisplaySize = (ImVec2){800.0f, 600.0f};
  io->DeltaTime = 0.1666667f;

  igNewFrame();
  return true;
}

void destroy_imgui_component(ImGuiComponent *self) {
  igDestroyContext(self->context);
  *self = (ImGuiComponent){0};
}

TB_DEFINE_COMPONENT(imgui, ImGuiComponent, void)

void tb_imgui_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "ImGui",
      .size = sizeof(ImGuiComponent),
      .id = ImGuiComponentId,
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_imgui_component,
      .destroy = tb_destroy_imgui_component,
  };
}
