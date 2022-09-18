#pragma once

#include <stdint.h>

#define OceanComponentId 0xBAD22222
#define OceanComponentIdStr "0xBAD22222"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct OceanComponentDescriptor {
  float tmp;
} OceanComponentDescriptor;

typedef struct OceanComponent {
  float tmp;
} OceanComponent;

void tb_ocean_component_descriptor(ComponentDescriptor *desc);
