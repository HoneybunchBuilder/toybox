#include "samplecore.h"

#include "world.h"

void tb_sample_on_start(TbWorld *world) {
  tb_load_scene(world, "scenes/Bistro.glb");
}
