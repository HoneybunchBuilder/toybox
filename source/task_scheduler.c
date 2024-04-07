#include "tb_task_scheduler.h"

#include "tbcommon.h"
#include "tbsystempriority.h"
#include "world.h"

#include <TaskScheduler_c.h>

ECS_COMPONENT_DECLARE(TbTaskScheduler);

typedef struct TbAsyncTaskArgs {
  TbAsyncFn fn;
  void *args;
} TbAsyncTaskArgs;

typedef struct TbTaskCompleteCleanupArgs {
  enkiTaskScheduler *enki;
  enkiTaskSet *task;
} TbTaskCompleteCleanupArgs;

void tb_async_task_complete(void *args, uint32_t threadnum) {
  (void)threadnum;
  tb_auto task_args = (TbTaskCompleteCleanupArgs *)args;
  enkiDeleteTaskSet(task_args->enki, task_args->task);
}

void tb_async_task_exec(uint32_t start, uint32_t end, uint32_t threadnum,
                        void *args) {
  TracyCZoneN(ctx, "Async Task", true);
  (void)start;
  (void)end;
  (void)threadnum;
  tb_auto task_args = (const TbAsyncTaskArgs *)args;
  task_args->fn(task_args->args);

  mi_free(task_args->args);
  mi_free(args);

  TracyCZoneEnd(ctx);
}

void tb_async_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                   size_t args_size) {
  TracyCZoneN(ctx, "Launch Async Task", true);
  tb_auto task = enkiCreateTaskSet(enki, tb_async_task_exec);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto fn_args = mi_malloc(args_size);
  SDL_memcpy(fn_args, args, args_size);

  tb_auto task_args = (TbAsyncTaskArgs *)mi_malloc(sizeof(TbAsyncTaskArgs));
  *task_args = (TbAsyncTaskArgs){fn, fn_args};
  enkiSetArgsTaskSet(task, task_args);

  tb_auto on_complete =
      enkiCreateCompletionAction(enki, NULL, tb_async_task_complete);
  tb_auto on_complete_args =
      (TbTaskCompleteCleanupArgs *)mi_malloc(sizeof(TbTaskCompleteCleanupArgs));
  *on_complete_args = (TbTaskCompleteCleanupArgs){enki, task};
  tb_auto on_complete_params = (struct enkiParamsCompletionAction){
      NULL,
      on_complete_args,
      enkiGetCompletableFromTaskSet(task),
  };
  enkiSetParamsCompletionAction(on_complete, on_complete_params);

  // Launch task
  enkiAddTaskSet(enki, task);
  TracyCZoneEnd(ctx);
}

void tb_main_thread_task_exec(void *args) {
  TracyCZoneN(ctx, "Main Thread Task", true);
  tb_auto task_args = (const TbAsyncTaskArgs *)args;
  task_args->fn(task_args->args);

  mi_free(task_args->args);
  mi_free(args);

  TracyCZoneEnd(ctx);
}

void tb_main_thread_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                         size_t args_size) {
  tb_auto task = enkiCreatePinnedTask(enki, tb_main_thread_task_exec, 0);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto fn_args = mi_malloc(args_size);
  SDL_memcpy(fn_args, args, args_size);

  tb_auto task_args = (TbAsyncTaskArgs *)mi_malloc(sizeof(TbAsyncTaskArgs));
  *task_args = (TbAsyncTaskArgs){fn, fn_args};
  enkiSetArgsPinnedTask(task, task_args);

  tb_auto on_complete =
      enkiCreateCompletionAction(enki, NULL, tb_async_task_complete);
  tb_auto on_complete_args =
      (TbTaskCompleteCleanupArgs *)mi_malloc(sizeof(TbTaskCompleteCleanupArgs));
  *on_complete_args = (TbTaskCompleteCleanupArgs){enki, task};
  tb_auto on_complete_params = (struct enkiParamsCompletionAction){
      NULL,
      on_complete_args,
      enkiGetCompletableFromPinnedTask(task),
  };
  enkiSetParamsCompletionAction(on_complete, on_complete_params);

  enkiAddPinnedTask(enki, task);
}

void tb_tick_pinned_tasks_sys(ecs_iter_t *it) {
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  enkiRunPinnedTasks(enki);
}

void tb_register_task_scheduler_sys(TbWorld *world) {
  ECS_COMPONENT_DEFINE(world->ecs, TbTaskScheduler);

  tb_auto enki = enkiNewTaskScheduler();
  enkiInitTaskScheduler(enki);

  ecs_singleton_set(world->ecs, TbTaskScheduler, {enki});

  ECS_SYSTEM(world->ecs, tb_tick_pinned_tasks_sys,
             EcsPostLoad, [inout] TbTaskScheduler(TbTaskScheduler));
}

void tb_unregister_task_scheduler_sys(TbWorld *world) {
  tb_auto enki = *ecs_singleton_get(world->ecs, TbTaskScheduler);

  enkiWaitforAllAndShutdown(enki);
  enkiDeleteTaskScheduler(enki);

  ecs_singleton_remove(world->ecs, TbTaskScheduler);
}

TB_REGISTER_SYS(tb, task_scheduler, TB_SYSTEM_HIGHEST)
