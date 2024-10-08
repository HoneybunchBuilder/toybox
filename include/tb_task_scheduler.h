#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <flecs.h>

#include <TaskScheduler_c.h>

typedef void (*TbAsyncFn)(const void *args);
typedef enkiTaskExecuteRange TbAsyncFn2;

typedef struct enkiTaskSet *TbTask;
typedef struct enkiPinnedTask *TbPinnedTask;
typedef struct enkiTaskScheduler *TbTaskScheduler;

extern ECS_COMPONENT_DECLARE(TbTask);
extern ECS_COMPONENT_DECLARE(TbPinnedTask);
extern ECS_COMPONENT_DECLARE(TbTaskScheduler);

#define TbMainThreadId 0

typedef struct TbAsyncTaskArgs TbAsyncTaskArgs;

void tb_run_pinned_tasks(ecs_world_t *ecs);

// Create a task that runs a given function on any available thread.
// Args will be copied to a thread-safe heap
// Task must be launched to begin execution
TbTask tb_create_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                      size_t args_size);

// Create a task that runs a given function on any available thread.
// Args will be passed directly to task
// Task must be launched to begin execution
TbTask tb_create_task2(TbTaskScheduler enki, TbAsyncFn2 fn, void *args);

// Begin execution of an already created task
void tb_launch_task(TbTaskScheduler enki, TbTask task);

void tb_launch_task_args(TbTaskScheduler enki, TbTask task, void *args,
                         size_t args_size);

// Begin execution of an already created task and also pass along arguments
void tb_launch_task2(TbTaskScheduler enki, TbTask task, void *args);

// Run a given function on any available thread.
// Args will be copied to a thread-safe heap
TbTask tb_async_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                     size_t args_size);

// Run a given function on the main thread.
// Args will be copied to a thread-safe heap
// Task will not run until manually launched
TbPinnedTask tb_create_pinned_task(TbTaskScheduler enki, TbAsyncFn fn,
                                   void *args, size_t args_size);

void tb_launch_pinned_task(TbTaskScheduler enki, TbPinnedTask task);
void tb_launch_pinned_task_args(TbTaskScheduler enki, TbPinnedTask task,
                                void *args, size_t size);

void tb_wait_task(TbTaskScheduler enki, TbTask task);

// Since pinned tasks are pumped manually by threads you will deadlock
// if you try to wait on a task pinned to the thread that must wait.
void tb_wait_pinned_task(TbTaskScheduler enki, TbPinnedTask task);

#ifdef __cplusplus
}
#endif
