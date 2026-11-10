#include <stdio.h>
#include <string.h>

#include "render_pool.h"

#define DEBUG_CLEAR_COLOUR 0xFFFF00FF

static void render_columns(const Render_Job* job, int x_start, int x_end)
{
  const Camera* camera = job->camera;
  e_render
  (
    job->map,
    camera->pos,
    camera->dir,
    camera->z,
    camera->fov,
    x_start,
    x_end,
    camera->sector,
    job->target.pixels,
    job->target.width,
    job->target.height,
    camera->pitch
  );
}

static int worker_main(void* data)
{
  Worker* worker = data;
  Render_Pool* pool = worker->pool;

  for (;;)
  {
    SDL_WaitSemaphore(worker->start);
    if (pool->quit) return 0;

    int width = pool->job.target.width;
    int x_start = worker->index * width / pool->count;
    int x_end = (worker->index + 1) * width / pool->count;
    render_columns(&pool->job, x_start, x_end);

    SDL_SignalSemaphore(pool->done);
  }
}

void render_pool_shutdown(Render_Pool* pool)
{
  pool->quit = 1;

  for (int i = 0; i < pool->count; i++)
  {
    Worker* worker = &pool->workers[i];
    if (worker->thread)
    {
      SDL_SignalSemaphore(worker->start);
      SDL_WaitThread(worker->thread, NULL);
    }
    if (worker->start) SDL_DestroySemaphore(worker->start);
  }

  if (pool->done) SDL_DestroySemaphore(pool->done);
  memset(pool, 0, sizeof(*pool));
}

int render_pool_init(Render_Pool* pool)
{
  memset(pool, 0, sizeof(*pool));

  int count = SDL_GetNumLogicalCPUCores();
  if (count < 1) count = 1;
  if (count > MAX_WORKERS) count = MAX_WORKERS;

  pool->done = SDL_CreateSemaphore(0);
  if (!pool->done) goto fail;

  for (int i = 0; i < count; i++)
  {
    Worker* worker = &pool->workers[i];
    worker->pool = pool;
    worker->index = i;

    worker->start = SDL_CreateSemaphore(0);
    pool->count = i + 1;
    if (!worker->start) goto fail;

    worker->thread = SDL_CreateThread(worker_main, "render", worker);
    if (!worker->thread) goto fail;
  }

  return 0;

fail:
  fprintf(stderr, "render_pool_init: %s\n", SDL_GetError());
  render_pool_shutdown(pool);
  return -1;
}

double render_pool_frame(Render_Pool* pool, const E_Map* map, const Camera* camera, Render_Target target, const Render_Debug* debug)
{
  if (!pool || pool->count < 1 || !map || !camera || !target.pixels || target.width <= 0 || target.height <= 0)
  {
    fprintf(stderr, "render_pool_frame: invalid arguments\n");
    return -1.0;
  }

  size_t pixels = (size_t)target.width * target.height;
  uint32_t clear = debug ? DEBUG_CLEAR_COLOUR : 0;
  for (size_t i = 0; i < pixels; i++) target.pixels[i] = clear;

  Uint64 start = SDL_GetPerformanceCounter();

  if (camera->sector)
  {
    Render_Job job = {map, camera, target};

    if (debug)
    {
      e_debug_hook = debug->hook;
      e_debug_user = debug->user;
      render_columns(&job, 0, target.width);
      e_debug_hook = NULL;
      e_debug_user = NULL;
    }
    else
    {
      pool->job = job;
      for (int i = 0; i < pool->count; i++) SDL_SignalSemaphore(pool->workers[i].start);
      for (int i = 0; i < pool->count; i++) SDL_WaitSemaphore(pool->done);
      pool->job = (Render_Job){0};
    }
  }

  return (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
}
