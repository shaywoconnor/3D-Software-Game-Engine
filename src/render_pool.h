#ifndef RENDER_POOL_H
#define RENDER_POOL_H

#include <SDL3/SDL.h>

#include "engine.h"
#include "e_map.h"
#include "platform.h"

#define MAX_WORKERS 64

struct Camera
{
  E_Vec2f pos;
  E_Vec2f dir;
  float z;
  float fov;
  float pitch;
  E_Map_Sector* sector;
};
typedef struct Camera Camera;

struct Render_Pool;

struct Worker
{
  SDL_Thread* thread;
  SDL_Semaphore* start;
  int x_start;
  int x_end;
  struct Render_Pool* pool;
};
typedef struct Worker Worker;

struct Render_Pool
{
  Worker workers[MAX_WORKERS];
  int count;
  SDL_Semaphore* done;
  int quit;
  E_Map* map;
  const Camera* camera;
  Platform* platform;
};
typedef struct Render_Pool Render_Pool;

int render_pool_init(Render_Pool* pool, int width);
void render_pool_shutdown(Render_Pool* pool);
double render_pool_frame(Render_Pool* pool, E_Map* map, const Camera* camera, Platform* platform, int debug);

#endif
