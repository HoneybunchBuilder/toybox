#pragma once

typedef struct ImFontAtlas ImFontAtlas;
typedef struct ImGuiContext ImGuiContext;
typedef struct ImGuiIO ImGuiIO;

#define ImGuiComponentId 0xFBADBEEF

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct ImGuiComponentDescriptor {
  ImFontAtlas *font_atlas;
} ImGuiComponentDescriptor;

typedef struct ImGuiComponent {
  ImGuiContext *context;
} ImGuiComponent;

void tb_imgui_component_descriptor(ComponentDescriptor *desc);
