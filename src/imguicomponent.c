#include "imguicomponent.h"

#include "tbimgui.h"
#include "world.h"

bool create_imgui_component(ImGuiComponent *self,
                            const ImGuiComponentDescriptor *desc) {
  *self = (ImGuiComponent){
      .context = igCreateContext(desc->font_atlas),
  };
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
