#include "tb_shader_system.h"

#include "tb_common.h"
#include "tb_profiling.h"
#include "tb_task_scheduler.h"
#include "tb_world.h"

typedef VkPipeline TbPipeline;
ECS_COMPONENT_DECLARE(TbPipeline);
ECS_TAG_DECLARE(TbShaderCompiled);

typedef struct TbShaderCompleteArgs {
  ecs_world_t *ecs;
  ecs_entity_t ent;
  VkPipeline pipeline;
} TbShaderCompleteArgs;

void tb_shader_complete_task(const void *args) {
  TB_TRACY_SCOPE("Shader Complete Task");

  tb_auto complete_args = (TbShaderCompleteArgs *)args;

  tb_auto ecs = complete_args->ecs;
  tb_auto ent = complete_args->ent;
  ecs_set(ecs, ent, TbPipeline, {complete_args->pipeline});
  ecs_add_id(ecs, ent, TbShaderCompiled);
}

typedef struct TbShaderCompileTaskArgs {
  ecs_world_t *ecs;
  ecs_entity_t ent;
  TbTaskScheduler enki;
  TbPinnedTask main_thread_task;
  TbShaderCompileFn compile_fn;
  void *compile_args;
} TbShaderCompileTaskArgs;

void tb_shader_compile_task(const void *args) {
  TB_TRACY_SCOPE("Shader Compile Task");

  tb_auto task_args = (TbShaderCompileTaskArgs *)args;
  VkPipeline pipe = task_args->compile_fn(task_args->compile_args);

  tb_auto enki = task_args->enki;

  // Manually launch pinned task with override args
  TbShaderCompleteArgs complete_args = {task_args->ecs, task_args->ent, pipe};
  tb_launch_pinned_task_args(enki, task_args->main_thread_task, &complete_args,
                             sizeof(TbShaderCompleteArgs));

  // We're only responsible for the compile args
  tb_free(tb_global_alloc, task_args->compile_args);
}

TbShader tb_shader_load(ecs_world_t *ecs, TbShaderCompileFn compile_fn,
                        void *args, size_t args_size) {
  TB_TRACY_SCOPE("Create Shader Load Task");

  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  TbShader ent = ecs_new(ecs);
  ecs_set(ecs, ent, TbPipeline, {0});

  // Need to make a copy of the args into a thread-safe pool
  tb_auto compile_args = tb_alloc(tb_global_alloc, args_size);
  SDL_memcpy(compile_args, args, args_size);

  // Create main thread task
  // Pass the enki task to the compile task
  // It will launch the task with the correct args when necessary
  // Avoids needing to interact with the ecs to launch a task
  tb_auto complete_task =
      tb_create_pinned_task(enki, tb_shader_complete_task, NULL, 0);

  // Launch an async task
  // Attach task related components *to* the shader entity
  tb_auto task_args = (TbShaderCompileTaskArgs){
      ecs, ent, enki, complete_task, compile_fn, compile_args};
  tb_auto task = tb_async_task(enki, tb_shader_compile_task, &task_args,
                               sizeof(TbShaderCompileTaskArgs));

  ecs_set(ecs, ent, TbTask, {task});
  return ent;
}

void tb_shader_destroy(ecs_world_t *ecs, TbShader shader) {
  if (!ecs_has(ecs, shader, TbPipeline) || !tb_is_shader_ready(ecs, shader)) {
    return;
  }
  tb_auto rnd_sys = ecs_singleton_get(ecs, TbRenderSystem);
  tb_auto pipe = tb_shader_get_pipeline(ecs, shader);
  tb_rnd_destroy_pipeline(rnd_sys, pipe);

  ecs_delete(ecs, shader);
}

bool tb_is_shader_ready(ecs_world_t *ecs, TbShader shader) {
  return ecs_has_id(ecs, shader, TbShaderCompiled) &&
         tb_shader_get_pipeline(ecs, shader) != VK_NULL_HANDLE;
}

bool tb_wait_shader_ready(ecs_world_t *ecs, TbShader shader) {
  if (!tb_is_shader_ready(ecs, shader)) {
    // we *require* the imgui shader be ready by this point
    // so wait for it if necessary
    if (ecs_has(ecs, shader, TbTask)) {
      tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);
      tb_auto task = *ecs_get(ecs, shader, TbTask);
      tb_wait_task(enki, task);
      return true;
    }
  }
  return false;
}

VkPipeline tb_shader_get_pipeline(ecs_world_t *ecs, TbShader ent) {
  tb_auto pipe = ecs_get(ecs, ent, TbPipeline);
  if (pipe == NULL) {
    return VK_NULL_HANDLE;
  }
  return *pipe;
}

void tb_register_shader_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Shader Sys");
  ECS_COMPONENT_DEFINE(world->ecs, TbPipeline);
  ECS_TAG_DEFINE(world->ecs, TbShaderCompiled);
}

void tb_unregister_shader_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, shader, TB_SHADER_SYS_PRIO)
