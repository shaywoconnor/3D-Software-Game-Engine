#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "app.h"
#include "input.h"
#include "stats.h"
#include "engine.h"

#define WINDOW_TITLE "Game"
#define RENDER_WIDTH 752
#define RENDER_HEIGHT 452
#define WINDOW_SCALE 2
#define VSYNC 0

#define GAME_FOV 100.0f

#define GRAVITY 10.0f
#define JUMP_VELOCITY 3.2f
#define ACCELERATION 7.0f
#define AIR_ACCELERATION 6.0f
#define MAX_VELOCITY 3.0f
#define AIR_MAX_VELOCITY 0.5f
#define GROUND_FRICTION 4.0f
#define STOP_SPEED 1.5f
#define TURN_SPEED 3.0f
#define PLAYER_HEIGHT 0.5f
#define HEAD_ROOM 0.1f
#define GROUND_EPSILON 0.001f
#define MAX_DT 0.05f
#define MOUSE_SENSITIVITY 0.0025f
#define MAX_PITCH 0.27f

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

struct Player
{
  Camera camera;
  float angle;
  float radius;
  E_Vec2f velocity;
  E_Vec2f wish_direction;
  float vertical_velocity;
  float max_speed;
};

typedef struct Player Player;

int player_on_ground(const Player* player)
{
  if (!player->camera.sector) return 0;
  if (player->vertical_velocity > 0.0f) return 0;
  float eye_level = player->camera.sector->floor_z + PLAYER_HEIGHT;
  return player->camera.z <= eye_level + GROUND_EPSILON;
}

void player_flat_accelerate(Player* player, float wish_speed, float acceleration, float dt)
{
  float projected_velocity = e_vec2f_dot(player->velocity, player->wish_direction);
  float add_velocity = wish_speed - projected_velocity;
  if (add_velocity <= 0.0f) return;

  float accelerated_velocity = acceleration * player->max_speed * dt;
  if (accelerated_velocity > add_velocity)
  {
    accelerated_velocity = add_velocity;
  }

  player->velocity.x += player->wish_direction.x * accelerated_velocity;
  player->velocity.y += player->wish_direction.y * accelerated_velocity;
}

void player_air_accelerate(Player* player, float dt)
{
  player->vertical_velocity -= GRAVITY * dt;
  player_flat_accelerate(player, AIR_MAX_VELOCITY, AIR_ACCELERATION, dt);
}

void player_ground_accelerate(Player* player, float dt)
{
  float speed = e_vec2f_mag(player->velocity);
  if (speed != 0.0f)
  {
    float control = (speed < STOP_SPEED) ? STOP_SPEED : speed;
    float drop = control * GROUND_FRICTION * dt;
    float scale = ((speed - drop > 0.0f) ? speed - drop : 0.0f) / speed;
    player->velocity.x *= scale;
    player->velocity.y *= scale;
  }
  player_flat_accelerate(player, player->max_speed, ACCELERATION, dt);
}

void player_accelerate(Player* player, const Input* input, float dt)
{
  if (!player_on_ground(player))
  {
    player_air_accelerate(player, dt);
    return;
  }

  if (input->held & KEY_SPACE)
  {
    player->vertical_velocity = JUMP_VELOCITY;
    player_air_accelerate(player, dt);
    return;
  }
  player_ground_accelerate(player, dt);
}

void player_update(Player* player, const Input* input, float dt)
{
  float turn = !!(input->held & KEY_LEFT) - !!(input->held & KEY_RIGHT);
  player->angle += turn * TURN_SPEED * dt;

  E_Vec2f dir = {cosf(player->angle), sinf(player->angle)};
  E_Vec2f right = {dir.y, -dir.x};
  player->camera.dir = dir;

  float forward = !!(input->held & KEY_W) - !!(input->held & KEY_S);
  float strafe = !!(input->held & KEY_D) - !!(input->held & KEY_A);

  E_Vec2f wish = {dir.x * forward + right.x * strafe, dir.y * forward + right.y * strafe};
  float len = sqrtf(wish.x * wish.x + wish.y * wish.y);

  if (len > 0)
  {
    player->wish_direction.x = wish.x / len;
    player->wish_direction.y = wish.y / len;
  }
  else
  {
    player->wish_direction = (E_Vec2f){0, 0};
  }

  player_accelerate(player, input, dt);

  player->angle -= input->mouse_dx * MOUSE_SENSITIVITY;
  player->camera.pitch -= input->mouse_dy * MOUSE_SENSITIVITY;
  if (player->camera.pitch > MAX_PITCH) player->camera.pitch = MAX_PITCH;
  if (player->camera.pitch < -MAX_PITCH) player->camera.pitch = -MAX_PITCH;
}

void move(E_Map* map, Player* player, float dt)
{
  E_Vec2f new_pos;
  new_pos.x = player->camera.pos.x + player->velocity.x * dt;
  new_pos.y = player->camera.pos.y + player->velocity.y * dt;

  E_Map_Sector* new_sector = get_camera_sector(map, new_pos);
  if (new_sector || !player->camera.sector)
  {
    player->camera.pos = new_pos;
    player->camera.sector = new_sector;
  }
  else
  {
    player->velocity = (E_Vec2f){0, 0};
  }
  
  player->camera.z += player->vertical_velocity * dt;

  E_Map_Sector* sector = player->camera.sector;
  if (!sector) return;

  float ceiling_level = sector->ceiling_z - HEAD_ROOM;
  if (player->camera.z > ceiling_level)
  {
    player->camera.z = ceiling_level;
    if (player->vertical_velocity > 0.0f) player->vertical_velocity = 0.0f;
  }

  float eye_level = sector->floor_z + PLAYER_HEIGHT;
  if (player->camera.z < eye_level)
  {
    player->camera.z = eye_level;
    if (player->vertical_velocity < 0.0f) player->vertical_velocity = 0.0f;
  }
}

Player player_create(E_Map* map, E_Vec2f pos, float angle)
{
  Player player = {0};
  player.angle = angle;
  player.radius = 0.25f;
  player.max_speed = MAX_VELOCITY;
  player.camera.pos = pos;
  player.camera.dir = (E_Vec2f){cosf(angle), sinf(angle)};
  player.camera.fov = GAME_FOV * (3.14159265f / 180.0f);
  player.camera.sector = get_camera_sector(map, pos);
  if (player.camera.sector) player.camera.z = player.camera.sector->floor_z + PLAYER_HEIGHT;
  return player;
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s <map file>\n", argv[0]);
    return 1;
  }

  App app;
  if (app_init(&app, argv[1], WINDOW_TITLE, RENDER_WIDTH, RENDER_HEIGHT, WINDOW_SCALE, VSYNC) != 0) return 1;

  Input input = {0};
  Player player = player_create(&app.map, (E_Vec2f){0.0f, 0.0f}, 0.0f);

  uint64_t last = platform_time_ns();
  Frame_Stats stats;
  stats_init(&stats, last);
  const char* lines[2] = {stats.fps_text, stats.render_text};

  int mouse_captured = 1;

  for (;;)
  {
    input_update(&input);
    if (input.held & (KEY_QUIT | KEY_ESCAPE)) break;
    if (input.pressed & KEY_TAB)
    {
      mouse_captured = !mouse_captured;
      platform_set_mouse_captured(&app.platform, mouse_captured);
    }

    uint64_t now = platform_time_ns();
    float dt = (now - last) / 1e9f;
    last = now;
    if (dt > MAX_DT) dt = MAX_DT;

    player_update(&player, &input, dt);
    move(&app.map, &player, dt);

    double render_ms = render_pool_frame(&app.pool, &app.map, &player.camera, &app.platform, (input.pressed & KEY_F2) != 0);
    stats_record(&stats, now, render_ms);
    platform_present(&app.platform, lines, 2);
  }

  app_shutdown(&app);
  return 0;
}
