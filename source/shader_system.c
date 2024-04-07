#include "tb_shader_system.h"

#include "profiling.h"
#include "tb_task_scheduler.h"
#include "tbcommon.h"
#include "world.h"

#include <mimalloc.h>

typedef struct TbShader {
  VkPipeline pipeline;
} TbShader;
ECS_COMPONENT_DECLARE(TbShader);
ECS_TAG_DECLARE(TbShaderCompiled);

typedef struct TbShaderCompleteArgs {
  ecs_world_t *ecs;
  ecs_entity_t ent;
  VkPipeline pipeline;
} TbShaderCompleteArgs;

void tb_shader_complete_task(const void *args) {
  TracyCZoneN(ctx, "Shader Complete Task", true);

  tb_auto complete_args = (TbShaderCompleteArgs *)args;

  tb_auto ecs = complete_args->ecs;
  tb_auto ent = complete_args->ent;
  ecs_set(ecs, ent, TbShader, {complete_args->pipeline});
  ecs_add_id(ecs, ent, TbShaderCompiled);

  mi_free(complete_args);
  TracyCZoneEnd(ctx);
}

typedef struct TbShaderCompileTaskArgs {
  ecs_world_t *ecs;
  enkiTaskScheduler *enki;
  ecs_entity_t ent;
  TbShaderCompileFn compile_fn;
  void *compile_args;
} TbShaderCompileTaskArgs;

void tb_shader_compile_task(const void *args) {
  TracyCZoneN(ctx, "Shader Compile Task", true);

  tb_auto task_args = (TbShaderCompileTaskArgs *)args;
  VkPipeline pipe = task_args->compile_fn(task_args->compile_args);

  tb_auto ecs = task_args->ecs;
  tb_auto enki = task_args->enki;

  // Launch task on the main thread to mark this shader as compiled
  TbShaderCompleteArgs complete_args = {ecs, task_args->ent, pipe};
  tb_main_thread_task(enki, tb_shader_complete_task, &complete_args,
                      sizeof(TbShaderCompleteArgs));

  // We're only responsible for the compile args
  mi_free(task_args->compile_args);
  TracyCZoneEnd(ctx);
}

ecs_entity_t tb_shader_load(ecs_world_t *ecs, TbShaderCompileFn compile_fn,
                            void *args, size_t args_size) {
  TracyCZoneN(ctx, "Create Shader Load task", true);

  ecs_entity_t shader = ecs_new_entity(ecs, 0);
  ecs_set(ecs, shader, TbShader, {0});

  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  // Need to make a copy of the args into a thread-safe pool
  tb_auto compile_args = mi_malloc(args_size);
  SDL_memcpy(compile_args, args, args_size);

  // Launch an async task
  tb_auto task_args =
      (TbShaderCompileTaskArgs){ecs, enki, shader, compile_fn, compile_args};
  tb_async_task(enki, tb_shader_compile_task, &task_args,
                sizeof(TbShaderCompileTaskArgs));

  TracyCZoneEnd(ctx);
  return shader;
}

void tb_shader_destroy(ecs_world_t *ecs, ecs_entity_t shader) {
  if (!ecs_has(ecs, shader, TbShader) || !tb_is_shader_ready(ecs, shader)) {
    return;
  }
  tb_auto rnd_sys = ecs_singleton_get(ecs, TbRenderSystem);
  tb_auto pipe = tb_shader_get_pipeline(ecs, shader);
  tb_rnd_destroy_pipeline(rnd_sys, pipe);
}

bool tb_is_shader_ready(ecs_world_t *ecs, ecs_entity_t shader) {
  return ecs_has_id(ecs, shader, TbShaderCompiled);
}

VkPipeline tb_shader_get_pipeline(ecs_world_t *ecs, ecs_entity_t ent) {
  if (!tb_is_shader_ready(ecs, ent)) {
    return VK_NULL_HANDLE;
  }

  tb_auto shader = ecs_get(ecs, ent, TbShader);
  TB_CHECK_RETURN(shader, "Shader was unexpectedly null", VK_NULL_HANDLE);

  return shader->pipeline;
}

void tb_register_shader_sys(TbWorld *world) {
  ECS_COMPONENT_DEFINE(world->ecs, TbShader);
  ECS_TAG_DEFINE(world->ecs, TbShaderCompiled);
}

void tb_unregister_shader_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, shader, TB_SHADER_SYS_PRIO)
