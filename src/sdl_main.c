#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "engine.h"
#include "e_map.h"

#define WINDOW_TITLE "Engine"
#define RENDER_WIDTH 320
#define RENDER_HEIGHT 200
#define WINDOW_SCALE 4
#define VSYNC 0

#define GAME_FOV 100.0f

#define DEBUG_COLUMNS_PER_FRAME 1
#define DEBUG_CLEAR_COLOUR 0xFFFF00FF
#define DEBUG_DELAY 4

#define STATS_INTERVAL_NS 500000000ull

#define MAX_WORKERS 64

struct Platform
{
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
  uint32_t* framebuffer;
  int width;
  int height;
};
typedef struct Platform Platform;

struct Input
{
  int quit;
  int debug_frame;
  float forward;
  float strafe;
  float turn;
  float pitch;
  int reset;
};
typedef struct Input Input;

struct Player
{
  E_Vec2f pos;
  float angle;
  float eye_height;
  float fov;
  float move_speed;
  float turn_speed;
  E_Map_Sector* sector;
  float pitch;
  float look_speed;
  int moving;
};
typedef struct Player Player;

struct Frame_Stats
{
  Uint64 window_start;
  int frames;
  double render_ms_total;
  char fps_text[64];
  char render_text[64];
  char player_text[64];
};
typedef struct Frame_Stats Frame_Stats;

struct Debug_View
{
  Platform* platform;
  int calls;
};
typedef struct Debug_View Debug_View;

static void platform_shutdown(Platform* platform)
{
  free(platform->framebuffer);
  if (platform->texture) SDL_DestroyTexture(platform->texture);
  if (platform->renderer) SDL_DestroyRenderer(platform->renderer);
  if (platform->window) SDL_DestroyWindow(platform->window);
  SDL_Quit();
  memset(platform, 0, sizeof(*platform));
}

static int platform_init(Platform* platform, const char* title, int width, int height, int scale, int vsync)
{
  memset(platform, 0, sizeof(*platform));
  platform->width = width;
  platform->height = height;

  if (!SDL_Init(SDL_INIT_VIDEO))
  {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    goto fail;
  }

  platform->window = SDL_CreateWindow(title, width * scale, height * scale, 0);
  if (!platform->window)
  {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    goto fail;
  }

  platform->renderer = SDL_CreateRenderer(platform->window, NULL);
  if (!platform->renderer)
  {
    fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
    goto fail;
  }
  SDL_SetRenderVSync(platform->renderer, vsync);

  platform->texture = SDL_CreateTexture(platform->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!platform->texture)
  {
    fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
    goto fail;
  }
  SDL_SetTextureScaleMode(platform->texture, SDL_SCALEMODE_NEAREST);

  platform->framebuffer = malloc((size_t)width * height * sizeof(uint32_t));
  if (!platform->framebuffer)
  {
    fprintf(stderr, "out of memory allocating framebuffer\n");
    goto fail;
  }

  return 0;

fail:
  platform_shutdown(platform);
  return -1;
}

static void draw_text_shadowed(SDL_Renderer* renderer, float x, float y, const char* text)
{
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderDebugText(renderer, x + 1, y + 1, text);
  SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
  SDL_RenderDebugText(renderer, x, y, text);
}

static void stats_draw(const Frame_Stats* stats, SDL_Renderer* renderer)
{
  SDL_SetRenderScale(renderer, 2.0f, 2.0f);
  draw_text_shadowed(renderer, 4, 4, stats->fps_text);
  draw_text_shadowed(renderer, 4, 16, stats->render_text);
  draw_text_shadowed(renderer, 4, 28, stats->player_text);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
}

static void platform_present(Platform* platform, const Frame_Stats* stats)
{
  SDL_UpdateTexture(platform->texture, NULL, platform->framebuffer, platform->width * sizeof(uint32_t));
  SDL_RenderClear(platform->renderer);
  SDL_RenderTexture(platform->renderer, platform->texture, NULL, NULL);
  if (stats) stats_draw(stats, platform->renderer);
  SDL_RenderPresent(platform->renderer);
}

static Input read_input(void)
{
  Input input = {0};
  SDL_Event event;

  while (SDL_PollEvent(&event))
  {
    if (event.type == SDL_EVENT_QUIT) input.quit = 1;
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
      if (event.key.key == SDLK_ESCAPE) input.quit = 1;
      if (event.key.key == SDLK_F2) input.debug_frame = 1;
    }
  }

  const bool* keys = SDL_GetKeyboardState(NULL);

  if (keys[SDL_SCANCODE_LEFT]) input.turn += 1.0f;
  if (keys[SDL_SCANCODE_RIGHT]) input.turn -= 1.0f;
  if (keys[SDL_SCANCODE_W]) input.forward += 1.0f;
  if (keys[SDL_SCANCODE_S]) input.forward -= 1.0f;
  if (keys[SDL_SCANCODE_D]) input.strafe += 1.0f;
  if (keys[SDL_SCANCODE_A]) input.strafe -= 1.0f;
  if (keys[SDL_SCANCODE_UP]) input.pitch += 1.0f;
  if (keys[SDL_SCANCODE_DOWN]) input.pitch -= 1.0f;
  if (keys[SDL_SCANCODE_R]) input.reset = 1;

  return input;
}

static Player player_create(E_Vec2f pos, float angle)
{
  Player player = {0};
  player.pos = pos;
  player.angle = angle;
  player.eye_height = 0.5f;
  player.fov = GAME_FOV * (3.14159265f / 180.0f);
  player.move_speed = 3.0f;
  player.turn_speed = 4.0f;
  player.pitch = 0.0f;
  player.look_speed = 1.5f;
  player.moving = 0;
  return player;
}

static E_Vec2f player_dir(const Player* player)
{
  return (E_Vec2f){cosf(player->angle), sinf(player->angle)};
}

static float player_eye_z(const Player* player)
{
  return player->sector->floor_z + player->eye_height;
}

E_Map_Wall* line_seg_wall_intercept(const E_Vec2f line_seg_a, const E_Vec2f line_seg_b, E_Map_Sector* sector, E_Map* map)
{
  for (int w = sector->first_wall; w < sector->first_wall + sector->wall_count; w++)
  {
    E_Map_Wall* wall = map->walls + w;
    E_Vec2f wall_a = map->vertices[wall->v1];
    E_Vec2f wall_b = map->vertices[wall->v2];
    
    E_Vec2f r = {line_seg_b.x - line_seg_a.x, line_seg_b.y - line_seg_a.y};
    E_Vec2f s = {wall_b.x - wall_a.x, wall_b.y - wall_a.y};
    E_Vec2f q = {wall_a.x - line_seg_a.x, wall_a.y - line_seg_a.y};

    float denom = r.x * s.y - r.y * s.x;
    if (denom == 0.0f) continue;

    float t = (q.x * s.y - q.y * s.x) / denom;
    float u = (q.x * r.y - q.y * r.x) / denom;

    if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f)
    {
      return wall;
    }
  }
  return NULL;
}

static void player_update(Player* player, E_Map* map, const Input* input, float dt)
{
  if (input->reset)
  {
    player->pos = (E_Vec2f){0, 0};
    player->angle = 0;
    player->eye_height = 0.5f;
    player->fov = GAME_FOV * (3.14159265f / 180.0f);
    player->move_speed = 3.0f;
    player->turn_speed = 4.0f;
    player->pitch = 0.0f;
    player->look_speed = 1.5f;
    player->moving = 0;
    return;
  }
  player->angle += input->turn * player->turn_speed * dt;

  E_Vec2f dir = player_dir(player);
  E_Vec2f right = {dir.y, -dir.x};
  float step = player->move_speed * dt;
  player->pitch += input->pitch * player->look_speed * dt;
  if (player->pitch > 0.27f) player->pitch = 0.27f;
  if (player->pitch < -0.27f) player->pitch = -0.27f;

  E_Vec2f new_pos;
  new_pos.x = player->pos.x + (dir.x * input->forward + right.x * input->strafe) * step;
  new_pos.y = player->pos.y + (dir.y * input->forward + right.y * input->strafe) * step;
  E_Map_Sector* new_sector = get_camera_sector(map, new_pos);
  if (!player->sector)
  {
    player->pos = new_pos;
    player->sector = new_sector;
    return;
  }
  E_Map_Wall* wall = line_seg_wall_intercept(player->pos, new_pos, player->sector, map);
  if (!wall || wall->neighbour != -1)
  {
    if (new_sector)
    {
      if (new_sector->floor_z <= player->sector->floor_z + 0.5f) 
      {
        player->pos = new_pos;
        player->sector = new_sector;
      }
    }
  }
  player->moving = 0;
  if (input->forward) { player->moving = 1;}
}

static void debug_present(void* user)
{
  Debug_View* view = user;
  view->calls++;
  if (view->calls % DEBUG_COLUMNS_PER_FRAME) return;

  SDL_PumpEvents();
  platform_present(view->platform, NULL);
  SDL_Delay(DEBUG_DELAY);
}

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
  const Player* player;
  Platform* platform;
};
typedef struct Render_Pool Render_Pool;

static void render_columns(E_Map* map, const Player* player, Platform* platform, int x_start, int x_end)
{
  e_render
  (
    map,
    player->pos,
    player_dir(player),
    player_eye_z(player),
    player->fov + ((1*player->moving)*(3.14159265f / 180.0f)),
    x_start,
    x_end,
    player->sector,
    platform->framebuffer,
    platform->width,
    platform->height,
    player->pitch
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

    render_columns(pool->map, pool->player, pool->platform, worker->x_start, worker->x_end);
    SDL_SignalSemaphore(pool->done);
  }
}

static void pool_shutdown(Render_Pool* pool)
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

static int pool_init(Render_Pool* pool, int width)
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
  fprintf(stderr, "pool_init: %s\n", SDL_GetError());
  pool_shutdown(pool);
  return -1;
}

static double render_frame(Platform* platform, Render_Pool* pool, E_Map* map, const Player* player, int debug)
{
  size_t pixels = (size_t)platform->width * platform->height;
  uint32_t clear = debug ? DEBUG_CLEAR_COLOUR : 0;
  for (size_t i = 0; i < pixels; i++) platform->framebuffer[i] = clear;

  Uint64 start = SDL_GetPerformanceCounter();

  if (player->sector)
  {
    if (debug)
    {
      Debug_View debug_view = {platform, 0};
      e_debug_hook = debug_present;
      e_debug_user = &debug_view;
      render_columns(map, player, platform, 0, platform->width);
      e_debug_hook = NULL;
      e_debug_user = NULL;
    }
    else
    {
      pool->map = map;
      pool->player = player;
      pool->platform = platform;
      for (int i = 0; i < pool->count; i++) SDL_SignalSemaphore(pool->workers[i].start);
      for (int i = 0; i < pool->count; i++) SDL_WaitSemaphore(pool->done);
    }
  }

  return (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
}

static void stats_init(Frame_Stats* stats, Uint64 now)
{
  memset(stats, 0, sizeof(*stats));
  stats->window_start = now;
  snprintf(stats->fps_text, sizeof(stats->fps_text), "FPS: --");
  snprintf(stats->render_text, sizeof(stats->render_text), "e_render: --");
  snprintf(stats->player_text, sizeof(stats->player_text), "pos: --");
}

static void stats_record(Frame_Stats* stats, Uint64 now, double render_ms, const Player* player)
{
  stats->frames++;
  stats->render_ms_total += render_ms;

  Uint64 elapsed = now - stats->window_start;
  if (elapsed < STATS_INTERVAL_NS) return;

  double seconds = elapsed / 1e9;
  snprintf(stats->fps_text, sizeof(stats->fps_text), "FPS: %.0f  (%.2f ms)", stats->frames / seconds, seconds * 1000.0 / stats->frames);
  snprintf(stats->render_text, sizeof(stats->render_text), "e_render: %.2f ms", stats->render_ms_total / stats->frames);

  if (player->sector)
  {
    snprintf(stats->player_text, sizeof(stats->player_text), "pos: %.2f %.2f  eye: %.2f", player->pos.x, player->pos.y, player_eye_z(player));
  }
  else
  {
    snprintf(stats->player_text, sizeof(stats->player_text), "pos: %.2f %.2f  outside map", player->pos.x, player->pos.y);
  }

  stats->window_start = now;
  stats->frames = 0;
  stats->render_ms_total = 0.0;
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s <map file>\n", argv[0]);
    return 1;
  }

  E_Map map = {0};
  if (E_Map_Import(&map, argv[1], 0) != 0)
  {
    return 1;
  }

  Platform platform;
  if (platform_init(&platform, WINDOW_TITLE, RENDER_WIDTH, RENDER_HEIGHT, WINDOW_SCALE, VSYNC) != 0)
  {
    E_Map_Free(&map);
    return 1;
  }
  if (e_init(&map) != 0)
{
  platform_shutdown(&platform);
  E_Map_Free(&map);
  return 1;
}

  for (int i = 0; i < map.texture_count; i++)
  {
    E_Map_Texture_Load(&map.textures[i]);
  }

  Render_Pool pool;
  if (pool_init(&pool, platform.width) != 0)
  {
    platform_shutdown(&platform);
    E_Map_Free(&map);
    return 1;
  }

  Player player = player_create((E_Vec2f){0.0f, 0.0f}, 0.0f);
  player.sector = get_camera_sector(&map, player.pos);

  Uint64 last = SDL_GetTicksNS();
  Frame_Stats stats;
  stats_init(&stats, last);

  for (;;)
  {
    Input input = read_input();
    if (input.quit) break;

    Uint64 now = SDL_GetTicksNS();
    float dt = (now - last) / 1e9f;
    last = now;

    player_update(&player, &map, &input, dt);
    double render_ms = render_frame(&platform, &pool, &map, &player, input.debug_frame);
    stats_record(&stats, now, render_ms, &player);
    platform_present(&platform, &stats);
  }

  pool_shutdown(&pool);
  platform_shutdown(&platform);
  E_Map_Free(&map);
  return 0;
}
