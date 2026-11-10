#ifndef RENDER_POOL_H
#define RENDER_POOL_H

#include <stdint.h>

#include <SDL3/SDL.h>

#include "engine.h"
#include "e_map.h"

#define MAX_WORKERS 64

struct Render_Target
{
  uint32_t* pixels;
  int width;
  int height;
};
typedef struct Render_Target Render_Target;

struct Render_Debug
{
  E_Debug_Hook hook;
  void* user;
};
typedef struct Render_Debug Render_Debug;

struct Render_Job
{
  const E_Map* map;
  const Camera* camera;
  Render_Target target;
};
typedef struct Render_Job Render_Job;

struct Render_Pool;

struct Worker
{
  SDL_Thread* thread;
  SDL_Semaphore* start;
  int index;
  struct Render_Pool* pool;
};
typedef struct Worker Worker;

struct Render_Pool
{
  Worker workers[MAX_WORKERS];
  int count;
  SDL_Semaphore* done;
  int quit;
  Render_Job job;
};
typedef struct Render_Pool Render_Pool;

int render_pool_init(Render_Pool* pool);
void render_pool_shutdown(Render_Pool* pool);

double render_pool_frame(Render_Pool* pool, const E_Map* map, const Camera* camera, Render_Target target, const Render_Debug* debug);

#endif
