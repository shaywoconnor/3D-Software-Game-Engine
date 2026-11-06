#include <stdio.h>
#include <string.h>

#include "render_pool.h"

#define DEBUG_COLUMNS_PER_FRAME 1
#define DEBUG_CLEAR_COLOUR 0xFFFF00FF
#define DEBUG_DELAY 4

struct Debug_View
{
  Platform* platform;
  int calls;
};
typedef struct Debug_View Debug_View;

static void debug_present(void* user)
{
  Debug_View* view = user;
  view->calls++;
  if (view->calls % DEBUG_COLUMNS_PER_FRAME) return;

  SDL_PumpEvents();
  platform_present(view->platform, NULL, 0);
  SDL_Delay(DEBUG_DELAY);
}

static void render_columns(E_Map* map, const Camera* camera, Platform* platform, int x_start, int x_end)
{
  e_render
  (
    map,
    camera->pos,
    camera->dir,
    camera->z,
    camera->fov,
    x_start,
    x_end,
    camera->sector,
    platform->framebuffer,
    platform->width,
    platform->height,
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

    render_columns(pool->map, pool->camera, pool->platform, worker->x_start, worker->x_end);
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

int render_pool_init(Render_Pool* pool, int width)
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
    worker->x_start = i * width / count;
    worker->x_end = (i + 1) * width / count;

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

double render_pool_frame(Render_Pool* pool, E_Map* map, const Camera* camera, Platform* platform, int debug)
{
  size_t pixels = (size_t)platform->width * platform->height;
  uint32_t clear = debug ? DEBUG_CLEAR_COLOUR : 0;
  for (size_t i = 0; i < pixels; i++) platform->framebuffer[i] = clear;

  Uint64 start = SDL_GetPerformanceCounter();

  if (camera->sector)
  {
    if (debug)
    {
      Debug_View debug_view = {platform, 0};
      e_debug_hook = debug_present;
      e_debug_user = &debug_view;
      render_columns(map, camera, platform, 0, platform->width);
      e_debug_hook = NULL;
      e_debug_user = NULL;
    }
    else
    {
      pool->map = map;
      pool->camera = camera;
      pool->platform = platform;
      for (int i = 0; i < pool->count; i++) SDL_SignalSemaphore(pool->workers[i].start);
      for (int i = 0; i < pool->count; i++) SDL_WaitSemaphore(pool->done);
    }
  }

  return (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
}
