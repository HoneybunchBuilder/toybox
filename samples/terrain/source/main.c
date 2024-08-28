#include "samplecore.h"

#include "tb_world.h"

void tb_sample_on_start(TbWorld *world) {
  tb_load_scene(world, "scenes/terrain.glb");
}
