#pragma once

#include <flecs.h>

typedef ecs_entity_t TbMesh2;

typedef struct TbMeshComponent {
  TbMesh2 mesh2;
} TbMeshComponent;
extern ECS_COMPONENT_DECLARE(TbMeshComponent);
