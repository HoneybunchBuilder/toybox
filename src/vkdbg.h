#pragma once

#include "simd.h"
#include <stdint.h>

typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkDevice_T *VkDevice;

void queue_begin_label(VkQueue queue, const char *label, float4 color);
void queue_end_label(VkQueue queue);

void cmd_begin_label(VkCommandBuffer cmd, const char *label, float4 color);
void cmd_end_label(VkCommandBuffer cmd);

void set_vk_name(VkDevice device, uint64_t object, uint32_t type,
                 const char *name);
void set_vk_tag(VkDevice device, uint64_t object, uint32_t type,
                uint64_t tag_id, uint64_t tag_size, const void *tag);
