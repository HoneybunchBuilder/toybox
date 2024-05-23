#pragma once

/*
  Beginning of a sketch for an idea of how to implement async loading.

  World is just a database that we fill by loading a glb and converting nodes to
  entities
  So loading could look like:
  Frame 0: Main thread gathers load requests
  Frame 1: Loading thread is signaled by main thread to load glbs & begins load
  Frame 2: Main thread continues; loading thread may occasionally update
  completion until frame A
  Frame A: Loading thread has created a new World table that can be appended to
  the main world database. Main thread takes time to append loading thread's
  payload onto the World and call any relevant callbacks for the newly created
  objects

  Downside, one of the more expensive parts of loading a scene is making calls
  to things like component's create and on_loaded function pointers. However
  doing this work on the main thread means that it naturally syncs with work
  that create or on_loaded might create for the render thread. Also it is
  possible to update the world in chunks per frame, say only 5 entities are
  added per frame to spread the load over time without sacrificing total frame
  time.
*/

#include "tb_allocator.h"

typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Semaphore SDL_Semaphore;
typedef struct SDL_mutex SDL_mutex;

typedef struct TbWorld TbWorld;

typedef struct TbLoadThread {
  SDL_Thread *thread;

  TbGeneralAllocator gp_alloc;
  TbArenaAllocator loading_arena;
} TbLoadThread;

// Copy the request list and tell the load thread to begin loading
void tb_signal_begin_load(TbLoadThread *thread, char const *const *scene_paths,
                          uint32_t scene_count);

// Check to see if loading is complete
bool tb_load_complete(TbLoadThread *thread, float *percent);

// Returns a pointer to a const World* that points to a chunk of loadthread
// owned memory that the main thread needs to copy and then decide how to append
void tb_on_world_loaded(TbLoadThread *thread, TbWorld const **world);
