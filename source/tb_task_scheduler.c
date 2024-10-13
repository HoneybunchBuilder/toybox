#include "tb_task_scheduler.h"

#include "tb_common.h"
#include "tb_system_priority.h"
#include "tb_world.h"

#include <TaskScheduler_c.h>

void *tb_ts_alloc(size_t size) {
  void *ptr = tb_alloc(tb_global_alloc, size);
  // TracyCAllocN(ptr, size, "Task Alloc");
  return ptr;
}

void tb_ts_free(void *ptr) {
  tb_free(tb_global_alloc, ptr);
  // TracyCFreeN(ptr, "Task Alloc");
}

#define tb_ts_alloc_tp(T) (T *)tb_ts_alloc(sizeof(T))

ECS_COMPONENT_DECLARE(TbTask);
ECS_COMPONENT_DECLARE(TbPinnedTask);
ECS_COMPONENT_DECLARE(TbTaskScheduler);

typedef struct TbAsyncTaskArgs {
  TbAsyncFn fn;
  void *args;
} TbAsyncTaskArgs;

typedef struct TbTaskCompleteCleanupArgs {
  TbTaskScheduler enki;
  TbTask task;
  void *args;
  enkiCompletionAction *complete;
} TbTaskCompleteCleanupArgs;

typedef struct TbPinnedTaskCompleteCleanupArgs {
  TbTaskScheduler enki;
  TbPinnedTask task;
  void *args;
  enkiCompletionAction *complete;
} TbPinnedTaskCompleteCleanupArgs;

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

void tb_task_exec(const TbAsyncTaskArgs *args) {
  args->fn(args->args);
  tb_ts_free(args->args);
}

void tb_async_task_exec(uint32_t start, uint32_t end, uint32_t threadnum,
                        void *args) {
  TB_TRACY_SCOPE("Async Task");
  (void)start;
  (void)end;
  (void)threadnum;
  tb_auto task_args = (TbAsyncTaskArgs *)args;
  tb_task_exec(task_args);
  tb_ts_free(task_args);
}

void tb_pinned_task_exec(void *args) {
  TB_TRACY_SCOPE("Pinned Task");
  tb_auto task_args = (TbAsyncTaskArgs *)args;
  tb_task_exec(task_args);
  tb_ts_free(task_args);
}

TbTask tb_create_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                      size_t args_size) {
  TB_TRACY_SCOPE("Create Async Task");
  tb_auto task = enkiCreateTaskSet(enki, tb_async_task_exec);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto task_args = tb_alloc_task_args(fn, args, args_size);
  enkiSetArgsTaskSet(task, task_args);
  return task;
}

TbTask tb_create_task2(TbTaskScheduler enki, TbAsyncFn2 fn, void *args) {
  TB_TRACY_SCOPE("Create Async Task2");

  tb_auto task = enkiCreateTaskSet(enki, fn);
  enkiSetArgsTaskSet(task, args);
  return task;
}

void tb_launch_task(TbTaskScheduler enki, TbTask task) {
  tb_launch_task_args(enki, task, NULL, 0);
}

void tb_launch_task_args(TbTaskScheduler enki, TbTask task, void *args,
                         size_t size) {
  if (args && size > 0) {
    tb_auto task_args = (TbAsyncTaskArgs *)enkiGetParamsTaskSet(task).pArgs;

    // If args were provided free previous args and allocate new ones
    if (task_args->args) {
      tb_ts_free(task_args->args);
    }
    task_args->args = tb_ts_alloc(size);
    SDL_memcpy(task_args->args, args, size);

    enkiSetArgsTaskSet(task, task_args);
  }

  enkiAddTaskSet(enki, task);
}

void tb_launch_task2(TbTaskScheduler enki, TbTask task, void *args) {
  enkiSetArgsTaskSet(task, args);
  tb_launch_task(enki, task);
}

TbTask tb_async_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                     size_t args_size) {
  TbTask task = tb_create_task(enki, fn, args, args_size);
  tb_launch_task(enki, task);
  return task;
}

TbPinnedTask tb_create_pinned_task(TbTaskScheduler enki, TbAsyncFn fn,
                                   void *args, size_t args_size) {
  TB_TRACY_SCOPE("Create Pinned Task");
  tb_auto task = enkiCreatePinnedTask(enki, tb_pinned_task_exec, 0);

  // Arguments need to be on the correct mimalloc heap so we copy them
  tb_auto task_args = tb_alloc_task_args(fn, args, args_size);
  enkiSetArgsPinnedTask(task, task_args);
  return task;
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

void tb_run_pinned_tasks(ecs_iter_t *it) {
  TB_TRACY_SCOPEC("Run Pinned Tasks", TracyCategoryColorCore)
  tb_auto enki = *ecs_field(it, TbTaskScheduler, 0);
  enkiRunPinnedTasks(enki);
}

void tb_wait_task(TbTaskScheduler enki, enkiTaskSet *task) {
  if (!enkiIsTaskSetComplete(enki, task)) {
    TB_TRACY_SCOPEC("Wait for Task", TracyCategoryColorWait);
    enkiWaitForTaskSet(enki, task);
  }
}

void tb_wait_pinned_task(TbTaskScheduler enki, enkiPinnedTask *task) {
  if (!enkiIsPinnedTaskComplete(enki, task)) {
    TB_TRACY_SCOPEC("Wait for Pinned Task", TracyCategoryColorWait);
    enkiWaitForPinnedTask(enki, task);
  }
}

void tb_register_task_scheduler_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Task Scheduler Sys");
  ECS_COMPONENT_DEFINE(world->ecs, TbTask);
  ECS_COMPONENT_DEFINE(world->ecs, TbPinnedTask);
  ECS_COMPONENT_DEFINE(world->ecs, TbTaskScheduler);

  tb_auto ecs = world->ecs;
  tb_auto enki = enkiNewTaskScheduler();
  enkiInitTaskScheduler(enki);

  ecs_singleton_set(ecs, TbTaskScheduler, {enki});

  // tb_run_pinned_tasks must be immediate because it can enqueue load
  // requests
  ecs_system(ecs, {
                      .entity = ecs_entity(
                          ecs, {.name = "tb_run_pinned_tasks",
                                .add = ecs_ids(ecs_dependson(EcsPreFrame))}),
                      .query.terms =
                          {
                              {.id = ecs_id(TbTaskScheduler),
                               .src.id = ecs_id(TbTaskScheduler)},
                          },
                      .callback = tb_run_pinned_tasks,
                      .immediate = true,
                  });
}

void tb_unregister_task_scheduler_sys(TbWorld *world) {
  tb_auto enki = *ecs_singleton_get(world->ecs, TbTaskScheduler);

  enkiWaitforAllAndShutdown(enki);
  enkiDeleteTaskScheduler(enki);

  ecs_singleton_remove(world->ecs, TbTaskScheduler);
}

TB_REGISTER_SYS(tb, task_scheduler, TB_SYSTEM_HIGHEST)
