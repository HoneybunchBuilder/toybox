#pragma once

#include <flecs.h>

#include <TaskScheduler_c.h>

typedef void (*TbAsyncFn)(const void *args);

typedef struct enkiTaskScheduler *TbTaskScheduler;
extern ECS_COMPONENT_DECLARE(TbTaskScheduler);

typedef struct TbAsyncTaskArgs TbAsyncTaskArgs;

// Run a given function on any available thread.
// Args will be copied to a thread-safe heap
// out_task is an optional out variable in case you want to interact
// with the enki task directly
ecs_entity_t tb_async_task(ecs_world_t *ecs, TbAsyncFn fn, void *args,
                           size_t args_size, enkiTaskSet **out_task);

// Run a given function on the main thread.
// Args will be copied to a thread-safe heap
// out_task is an optional out variable in case you want to interact
// with the enki pinned task directly
// Task will not run until manually launched
ecs_entity_t tb_create_pinned_task(ecs_world_t *ecs, TbAsyncFn fn, void *args,
                                   size_t args_size, enkiPinnedTask **out_task);

void tb_launch_pinned_task(enkiTaskScheduler *enki, enkiPinnedTask *task);
void tb_launch_pinned_task_args(enkiTaskScheduler *enki, enkiPinnedTask *task,
                                void *args, size_t size);

// For waiting on a task via the ECS interface. This isn't usable
// if you just launched a task. Use `tb_wait_task_set` or `tb_wait_pinned_task`.
void tb_task_wait(ecs_world_t *ecs, ecs_entity_t task);

// For waiting directly on an enki task
void tb_wait_task_set(ecs_world_t *ecs, enkiTaskSet *task);

// For waiting directly on a pinned enki task
// Since pinned tasks are pumped manually by threads you will deadlock
// if you try to wait on a task pinned to the thread that must wait.
void tb_wait_pinned_task(ecs_world_t *ecs, enkiPinnedTask *task);
