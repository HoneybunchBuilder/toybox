#include "samplecore.h"

#include "world.h"

void tb_sample_on_start(World *world) {
  tb_world_load_scene(world, "scenes/Bistro.glb");
}

void tb_sample_on_start2(TbWorld *world) {
  tb_load_scene2(world, "scenes/Bistro.glb");
}
