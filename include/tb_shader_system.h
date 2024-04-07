#pragma once

#include "rendersystem.h"

#include <flecs.h>

#define TB_SHADER_SYS_PRIO (TB_RND_SYS_PRIO + 1)

typedef struct VkPipeline_T *VkPipeline;

typedef VkPipeline (*TbShaderCompileFn)(void *args);

ecs_entity_t tb_shader_load(ecs_world_t *ecs, TbShaderCompileFn create_fn,
                            void *args, size_t args_size);
void tb_shader_destroy(ecs_world_t *ecs, ecs_entity_t shader);

bool tb_is_shader_ready(ecs_world_t *ecs, ecs_entity_t shader);
VkPipeline tb_shader_get_pipeline(ecs_world_t *ecs, ecs_entity_t shader);
