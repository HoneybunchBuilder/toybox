#pragma once

#include <stdbool.h>

#define CoreUIComponentId 0xDEAD10CC

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct CoreUIComponent {
  bool show_about;
  bool show_metrics;
} CoreUIComponent;

void tb_coreui_component_descriptor(ComponentDescriptor *desc);
