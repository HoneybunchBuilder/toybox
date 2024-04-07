#include "tb_task_scheduler.h"

#include "tbcommon.h"
#include "tbsystempriority.h"
#include "world.h"

#include <TaskScheduler_c.h>

void *tb_ts_alloc(size_t size) {
  void *ptr = mi_malloc(size);
  TracyCAllocN(ptr, size, "Task Alloc");
  return ptr;
}

void tb_ts_free(void *ptr) {
  mi_free(ptr);
  TracyCFreeN(ptr, "Task Alloc");
}

ECS_COMPONENT_DECLARE(TbTaskScheduler);

typedef struct enkiTaskSet *TbTask;
ECS_COMPONENT_DECLARE(TbTask);
typedef struct enkiCompleteAction *TbCompleteAction;
ECS_COMPONENT_DECLARE(TbCompleteAction);
ECS_TAG_DECLARE(TbTaskComplete);
ECS_TAG_DECLARE(TbTaskPinned);

typedef struct TbAsyncTaskArgs {
  TbAsyncFn fn;
  void *args;
} TbAsyncTaskArgs;

typedef struct TbTaskCompleteCleanupArgs {
  ecs_world_t *ecs;
  ecs_entity_t task_ent;
} TbTaskCompleteCleanupArgs;

void tb_async_task_complete(void *args, uint32_t threadnum) {
  (void)threadnum;
  tb_auto task_args = (TbTaskCompleteCleanupArgs *)args;
  tb_auto ecs = task_args->ecs;

  // Mark task to be cleaned up later
  ecs_add_id(ecs, task_args->task_ent, TbTaskComplete);

  tb_ts_free(args);
}

void tb_task_exec(const TbAsyncTaskArgs *args) {
  args->fn(args->args);
  // Free up task arguments since we own this copy of what the user provided
  tb_ts_free(args->args);
}

void tb_async_task_exec(uint32_t start, uint32_t end, uint32_t threadnum,
                        void *args) {
  TracyCZoneN(ctx, "Async Task", true);
  (void)start;
  (void)end;
  (void)threadnum;
  tb_auto task_args = (const TbAsyncTaskArgs *)args;
  tb_task_exec(task_args);
  tb_ts_free(args);
  TracyCZoneEnd(ctx);
}

void tb_pinned_task_exec(void *args) {
  TracyCZoneN(ctx, "Pinned Task", true);
  tb_auto task_args = (const TbAsyncTaskArgs *)args;
  tb_task_exec(task_args);
  tb_ts_free(args);
  TracyCZoneEnd(ctx);
}

ecs_entity_t tb_async_task(ecs_world_t *ecs, TbAsyncFn fn, void *args,
                           size_t args_size, enkiTaskSet **out_task) {
  TracyCZoneN(ctx, "Launch Async Task", true);

  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  tb_auto task = enkiCreateTaskSet(enki, tb_async_task_exec);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto fn_args = tb_ts_alloc(args_size);
  SDL_memcpy(fn_args, args, args_size);

  tb_auto task_args = (TbAsyncTaskArgs *)tb_ts_alloc(sizeof(TbAsyncTaskArgs));
  *task_args = (TbAsyncTaskArgs){fn, fn_args};
  enkiSetArgsTaskSet(task, task_args);

  tb_auto on_complete =
      enkiCreateCompletionAction(enki, NULL, tb_async_task_complete);

  // Create an entity that represents the task
  tb_auto task_ent = ecs_new_entity(ecs, 0);
  ecs_set(ecs, task_ent, TbTask, {task});
  ecs_set(ecs, task_ent, TbCompleteAction, {(TbCompleteAction)on_complete});

  tb_auto on_complete_args = (TbTaskCompleteCleanupArgs *)tb_ts_alloc(
      sizeof(TbTaskCompleteCleanupArgs));
  *on_complete_args = (TbTaskCompleteCleanupArgs){ecs, task_ent};
  tb_auto on_complete_params = (struct enkiParamsCompletionAction){
      NULL,
      on_complete_args,
      enkiGetCompletableFromTaskSet(task),
  };
  enkiSetParamsCompletionAction(on_complete, on_complete_params);

  if (out_task) {
    *out_task = task;
  }

  // Launch task
  enkiAddTaskSet(enki, task);

  TracyCZoneEnd(ctx);
  return task_ent;
}

ecs_entity_t tb_main_thread_task(ecs_world_t *ecs, TbAsyncFn fn, void *args,
                                 size_t args_size, enkiPinnedTask **out_task) {
  TracyCZoneN(ctx, "Launch Main Task", true);
  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  tb_auto task = enkiCreatePinnedTask(enki, tb_pinned_task_exec, 0);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto fn_args = tb_ts_alloc(args_size);
  SDL_memcpy(fn_args, args, args_size);

  tb_auto task_args = (TbAsyncTaskArgs *)tb_ts_alloc(sizeof(TbAsyncTaskArgs));
  *task_args = (TbAsyncTaskArgs){fn, fn_args};
  enkiSetArgsPinnedTask(task, task_args);

  tb_auto on_complete =
      enkiCreateCompletionAction(enki, NULL, tb_async_task_complete);

  // Create an entity that represents the task
  tb_auto task_ent = ecs_new_entity(ecs, 0);
  ecs_set(ecs, task_ent, TbTask, {(TbTask)task});
  ecs_set(ecs, task_ent, TbCompleteAction, {(TbCompleteAction)on_complete});
  ecs_add_id(ecs, task_ent, TbTaskPinned);

  tb_auto on_complete_args = (TbTaskCompleteCleanupArgs *)tb_ts_alloc(
      sizeof(TbTaskCompleteCleanupArgs));
  *on_complete_args = (TbTaskCompleteCleanupArgs){ecs, task_ent};
  tb_auto on_complete_params = (struct enkiParamsCompletionAction){
      NULL,
      on_complete_args,
      enkiGetCompletableFromPinnedTask(task),
  };
  enkiSetParamsCompletionAction(on_complete, on_complete_params);

  if (out_task) {
    *out_task = task;
  }

  enkiAddPinnedTask(enki, task);
  TracyCZoneEnd(ctx);

  return task_ent;
}

void tb_task_wait(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);
  tb_auto task = *ecs_get(ecs, ent, TbTask);
  enkiWaitForTaskSet(enki, task);
}

void tb_run_pinned_tasks_sys(ecs_iter_t *it) {
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  enkiRunPinnedTasks(enki);
}

void tb_wait_task_set(ecs_world_t *ecs, enkiTaskSet *task) {
  enkiWaitForTaskSet(*ecs_singleton_get(ecs, TbTaskScheduler), task);
}

void tb_wait_pinned_task(ecs_world_t *ecs, enkiPinnedTask *task) {
  enkiWaitForPinnedTask(*ecs_singleton_get(ecs, TbTaskScheduler), task);
}

void tb_cleanup_completed_tasks_sys(ecs_iter_t *it) {
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 1);
  tb_auto tasks = ecs_field(it, TbTask, 2);
  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto entity = it->entities[i];

    if (!ecs_is_alive(it->world, entity)) {
      continue;
    }

    tb_auto task = tasks[i];

    if (ecs_has(it->world, entity, TbTaskPinned)) {
      tb_auto pinned_task = (struct enkiPinnedTask *)task;
      enkiDeletePinnedTask(enki, pinned_task);
      ecs_remove(it->world, entity, TbTaskPinned);
    } else {
      enkiDeleteTaskSet(enki, task);
    }

    if (ecs_has(it->world, entity, TbCompleteAction)) {
      tb_auto on_complete = *ecs_get(it->world, entity, TbCompleteAction);
      enkiDeleteCompletionAction(enki,
                                 (struct enkiCompletionAction *)on_complete);
      ecs_remove(it->world, entity, TbCompleteAction);
    }
    ecs_remove(it->world, entity, TbTask);
    ecs_remove(it->world, entity, TbTaskComplete);

    ecs_delete(it->world, entity);
  }
}

void tb_register_task_scheduler_sys(TbWorld *world) {
  ECS_COMPONENT_DEFINE(world->ecs, TbTaskScheduler);
  ECS_COMPONENT_DEFINE(world->ecs, TbTask);
  ECS_COMPONENT_DEFINE(world->ecs, TbCompleteAction);
  ECS_TAG_DEFINE(world->ecs, TbTaskComplete);
  ECS_TAG_DEFINE(world->ecs, TbTaskPinned);

  tb_auto enki = enkiNewTaskScheduler();
  enkiInitTaskScheduler(enki);

  ecs_singleton_set(world->ecs, TbTaskScheduler, {enki});

  ECS_SYSTEM(world->ecs, tb_run_pinned_tasks_sys,
             EcsPostLoad, [inout] TbTaskScheduler(TbTaskScheduler));

  ECS_SYSTEM(world->ecs, tb_cleanup_completed_tasks_sys,
             EcsPostLoad, [inout] TbTaskScheduler(TbTaskScheduler), [in] TbTask,
             [in] TbTaskComplete);
}

void tb_unregister_task_scheduler_sys(TbWorld *world) {
  tb_auto enki = *ecs_singleton_get(world->ecs, TbTaskScheduler);

  enkiWaitforAllAndShutdown(enki);
  enkiDeleteTaskScheduler(enki);

  ecs_singleton_remove(world->ecs, TbTaskScheduler);
}

TB_REGISTER_SYS(tb, task_scheduler, TB_SYSTEM_HIGHEST)
