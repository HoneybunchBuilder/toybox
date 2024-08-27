#pragma once

#include "tb_render_system.h"

#include <flecs.h>

#define TB_SHADER_SYS_PRIO (TB_RND_SYS_PRIO + 1)

typedef struct VkPipeline_T *VkPipeline;

typedef VkPipeline (*TbShaderCompileFn)(void *args);

typedef ecs_entity_t TbShader;

TbShader tb_shader_load(ecs_world_t *ecs, TbShaderCompileFn create_fn,
                        void *args, size_t args_size);
void tb_shader_destroy(ecs_world_t *ecs, TbShader shader);

bool tb_is_shader_ready(ecs_world_t *ecs, TbShader shader);
bool tb_wait_shader_ready(ecs_world_t *ecs, TbShader shader);

VkPipeline tb_shader_get_pipeline(ecs_world_t *ecs, TbShader shader);
