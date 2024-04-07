#pragma once

#include <flecs.h>

#include <TaskScheduler_c.h>

typedef struct enkiTaskScheduler *TbTaskScheduler;

extern ECS_COMPONENT_DECLARE(TbTaskScheduler);

typedef void (*TbAsyncFn)(const void *args);

// Fire and forget the given function with the given arguments on whatever
// thread is available. Will be cleaned up automatically.
// Does not return a pointer to the task set because it cannot be safely
// tested or waited on.
void tb_async_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                   size_t args_size);

void tb_main_thread_task(TbTaskScheduler enki, TbAsyncFn fn, void *args,
                         size_t args_size);
