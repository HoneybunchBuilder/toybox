#include "imguicomponent.h"

#include "tbimgui.h"
#include "world.h"

bool create_imgui_component(ImGuiComponent *self,
                            const ImGuiComponentDescriptor *desc) {
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

  // TODO: Actually create and upload this texture

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
  desc->name = "ImGui";
  desc->size = sizeof(ImGuiComponent);
  desc->id = ImGuiComponentId;
  desc->create = tb_create_imgui_component;
  desc->destroy = tb_destroy_imgui_component;
}
