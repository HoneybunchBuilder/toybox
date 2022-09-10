#pragma once

typedef struct RenderSystem RenderSystem;

#define SkyComponentId 0xCAFEB0BA

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct SkyComponentDescriptor {
  int tmp;
} SkyComponentDescriptor;

typedef struct SkyComponent {
  RenderSystem *render_system;
} SkyComponent;

void tb_sky_component_descriptor(ComponentDescriptor *desc);
