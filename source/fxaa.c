
/*
  Idea:
  We need a system to manage rendering with FXAA
  This also serves as an API touch-point for game code / other systems
  to adjust if/how FXAA is applied.

void record_fxaa(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                 uint32_t batch_count, const TbDispatchBatch *batches) {}

void tb_register_fxaa_system() {}

void tb_unregister_fxaa_system() {}

*/
