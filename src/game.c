#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "engine.h"
#include "input.h"
#include "player.h"
#include "stats.h"
#include "world.h"

#define GAME_MAX_DT 0.05f

struct Game_State
{
  Input input;
  Player player;
  Frame_Stats stats;
  World world;

  bool running;

  uint64_t now;
  uint64_t last;
  float dt;

  bool render_debug_animation;
};
typedef struct Game_State Game_State;

static int game_init(Game_State* state, const char* map_path)
{
  memset(state, 0, sizeof(*state));

  if (world_create(&state->world, map_path) != 0) return -1;

  if (player_create(&state->player, &state->world.map) != 0)
  {
    world_destroy(&state->world);
    return -1;
  }

  state->last = platform_time_ns();
  stats_create(&state->stats, state->last);
  state->running = true;
  return 0;
}

static void game_shutdown(Game_State* state)
{
  world_destroy(&state->world);
  memset(state, 0, sizeof(*state));
}

static void game_timer_start(Game_State* state)
{
  state->now = platform_time_ns();
  state->dt = (state->now - state->last) / 1e9f;
  state->last = state->now;
  if (state->dt > GAME_MAX_DT) state->dt = GAME_MAX_DT;
}

static void game_process_input(Game_State* state, Platform* platform)
{
  input_update(&state->input);

  if (state->input.held & (KEY_QUIT | KEY_ESCAPE)) state->running = false;
  if (state->input.pressed & KEY_TAB) platform_toggle_mouse_captured(platform);
  state->render_debug_animation = (state->input.pressed & KEY_F2) != 0;
}

static void game_update(Game_State* state)
{
  player_update(&state->player, &state->input, &state->world.map, state->dt);
}

static void game_timed_render(Game_State* state, App* app)
{
  Platform* platform = &app->platform;
  Render_Target target = {platform->framebuffer, platform->width, platform->height};
  Render_Debug debug = {platform_debug_present, platform};

  double render_ms = render_pool_frame
  (
    &app->pool,
    &state->world.map,
    &state->player.camera,
    target,
    state->render_debug_animation ? &debug : NULL
  );

  if (render_ms >= 0.0) stats_record(&state->stats, state->now, render_ms);
}

static void game_debug_display(Game_State* state, App* app)
{
  const char* lines[] = {state->stats.fps_text, state->stats.render_text};
  platform_present(&app->platform, lines, (int)(sizeof(lines) / sizeof(lines[0])));
}

static void game_loop(Game_State* state, App* app)
{
  while (state->running)
  {
    game_timer_start(state);
    game_process_input(state, &app->platform);
    if (!state->running) break;

    game_update(state);
    game_timed_render(state, app);
    game_debug_display(state, app);
  }
}

int game_play(App* app, const char* map_path)
{
  Game_State state;
  if (game_init(&state, map_path) != 0) return -1;
  game_loop(&state, app);
  game_shutdown(&state);
  return 0;
}
