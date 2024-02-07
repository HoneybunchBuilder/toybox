#include "transformercomponents.h"

#include "world.h"

#include <flecs.h>

ECS_COMPONENT_DECLARE(TbRotatorComponent);

void tb_register_rotator_component(TbWorld *world) {
  ECS_COMPONENT_DEFINE(world->ecs, TbRotatorComponent);
}
