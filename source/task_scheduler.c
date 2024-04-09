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

#define tb_ts_alloc_tp(T) (T *)tb_ts_alloc(sizeof(T))

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

TbAsyncTaskArgs *tb_alloc_task_args(TbAsyncFn fn, void *args, size_t size) {
  tb_auto fn_args = NULL;
  if (args && size > 0) {
    fn_args = tb_ts_alloc(size);
    SDL_memcpy(fn_args, args, size);
  }
  // Always alloc the task args wrapper
  tb_auto task_args = tb_ts_alloc_tp(TbAsyncTaskArgs);
  *task_args = (TbAsyncTaskArgs){fn, fn_args};

  return task_args;
}

void tb_free_task_args(TbAsyncTaskArgs *args) {
  tb_ts_free(args->args);
  args->args = NULL;
  tb_ts_free(args);
}

void tb_async_task_complete(void *args, uint32_t threadnum) {
  (void)threadnum;
  tb_auto task_args = (TbTaskCompleteCleanupArgs *)args;
  tb_auto ecs = task_args->ecs;

  // Mark task to be cleaned up later
  ecs_add_id(ecs, task_args->task_ent, TbTaskComplete);

  tb_ts_free(args);
}

void tb_task_exec(const TbAsyncTaskArgs *args) { args->fn(args->args); }

void tb_async_task_exec(uint32_t start, uint32_t end, uint32_t threadnum,
                        void *args) {
  TracyCZoneN(ctx, "Async Task", true);
  (void)start;
  (void)end;
  (void)threadnum;
  tb_auto task_args = (TbAsyncTaskArgs *)args;
  tb_task_exec(task_args);
  tb_free_task_args(task_args);
  TracyCZoneEnd(ctx);
}

void tb_pinned_task_exec(void *args) {
  TracyCZoneN(ctx, "Pinned Task", true);
  tb_auto task_args = (TbAsyncTaskArgs *)args;
  tb_task_exec(task_args);
  tb_free_task_args(task_args);
  TracyCZoneEnd(ctx);
}

ecs_entity_t tb_async_task(ecs_world_t *ecs, TbAsyncFn fn, void *args,
                           size_t args_size, enkiTaskSet **out_task) {
  TracyCZoneN(ctx, "Launch Async Task", true);
  ecs_defer_begin(ecs);

  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  tb_auto task = enkiCreateTaskSet(enki, tb_async_task_exec);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto task_args = tb_alloc_task_args(fn, args, args_size);
  enkiSetArgsTaskSet(task, task_args);

  tb_auto on_complete =
      enkiCreateCompletionAction(enki, NULL, tb_async_task_complete);

  // Create an entity that represents the task
  tb_auto task_ent = ecs_new_entity(ecs, 0);
  ecs_set(ecs, task_ent, TbTask, {task});
  ecs_set(ecs, task_ent, TbCompleteAction, {(TbCompleteAction)on_complete});

  tb_auto on_complete_args = tb_ts_alloc_tp(TbTaskCompleteCleanupArgs);
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

  ecs_defer_end(ecs);
  TracyCZoneEnd(ctx);
  return task_ent;
}

ecs_entity_t tb_create_pinned_task(ecs_world_t *ecs, TbAsyncFn fn, void *args,
                                   size_t args_size,
                                   enkiPinnedTask **out_task) {
  TracyCZoneN(ctx, "Create Main Task", true);
  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  tb_auto task = enkiCreatePinnedTask(enki, tb_pinned_task_exec, 0);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto task_args = tb_alloc_task_args(fn, args, args_size);
  enkiSetArgsPinnedTask(task, task_args);

  tb_auto on_complete =
      enkiCreateCompletionAction(enki, NULL, tb_async_task_complete);

  // Create an entity that represents the task
  tb_auto task_ent = ecs_new_entity(ecs, 0);
  ecs_set(ecs, task_ent, TbTask, {(TbTask)task});
  ecs_set(ecs, task_ent, TbCompleteAction, {(TbCompleteAction)on_complete});
  ecs_add_id(ecs, task_ent, TbTaskPinned);

  tb_auto on_complete_args = tb_ts_alloc_tp(TbTaskCompleteCleanupArgs);
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

  TracyCZoneEnd(ctx);

  return task_ent;
}

void tb_launch_pinned_task(enkiTaskScheduler *enki, enkiPinnedTask *task) {
  // If task was given args on creation we will not override them
  tb_launch_pinned_task_args(enki, task, NULL, 0);
}

void tb_launch_pinned_task_args(enkiTaskScheduler *enki, enkiPinnedTask *task,
                                void *args, size_t size) {
  tb_auto task_args = (TbAsyncTaskArgs *)enkiGetParamsPinnedTask(task).pArgs;

  if (args && size > 0) {
    // If args were provided free previous args and allocate new ones
    if (task_args->args) {
      tb_ts_free(task_args->args);
    }
    task_args->args = tb_ts_alloc(size);
    SDL_memcpy(task_args->args, args, size);
  }

  enkiAddPinnedTaskArgs(enki, task, task_args);
}

void tb_task_wait(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);
  tb_auto task = ecs_get(ecs, ent, TbTask);
  if (task) {
    if (ecs_has(ecs, ent, TbTaskComplete)) {
      return;
    }
    if (ecs_has(ecs, ent, TbTaskPinned)) {
      enkiWaitForPinnedTask(enki, (enkiPinnedTask *)*task);
    } else {
      enkiWaitForTaskSet(enki, *task);
    }
  }
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
