#include "samplecore.h"

#include "world.h"

void tb_sample_on_start(World *world) {
  tb_world_load_scene(world, "scenes/Components.glb");
}
