#include "tbrendercommon.h"

void tb_record_fullscreen(VkCommandBuffer buffer, const TbDrawBatch *batch,
                          const TbFullscreenBatch *fs_batch) {
  // Just drawing a fullscreen triangle
  vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
  vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
  vkCmdSetScissor(buffer, 0, 1, &batch->scissor);
  vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          batch->layout, 0, 1, &fs_batch->set, 0, NULL);
  vkCmdDraw(buffer, 3, 1, 0, 0);
}
