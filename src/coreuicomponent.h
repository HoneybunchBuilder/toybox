#pragma once

#define CoreUIComponentId 0xDEAD10CC

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct CoreUIComponent {
  int test;
} CoreUIComponent;

void tb_coreui_component_descriptor(ComponentDescriptor *desc);
