#include <math.h>
#include <stdio.h>
#include <string.h>

#include "player.h"

#define PLAYER_FOV 100.0f
#define PLAYER_RADIUS 0.25f

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
#define MOUSE_SENSITIVITY 0.0025f
#define MAX_PITCH 0.27f

static int player_on_ground(const Player* player)
{
  if (!player->camera.sector) return 0;
  if (player->vertical_velocity > 0.0f) return 0;
  float eye_level = player->camera.sector->floor_z + PLAYER_HEIGHT;
  return player->camera.z <= eye_level + GROUND_EPSILON;
}

static void player_flat_accelerate(Player* player, float wish_speed, float acceleration, float dt)
{
  float projected_velocity = e_vec2f_dot(player->velocity, player->wish_direction);
  float add_velocity = wish_speed - projected_velocity;
  if (add_velocity <= 0.0f) return;

  float accelerated_velocity = acceleration * player->max_speed * dt;
  if (accelerated_velocity > add_velocity) accelerated_velocity = add_velocity;

  player->velocity.x += player->wish_direction.x * accelerated_velocity;
  player->velocity.y += player->wish_direction.y * accelerated_velocity;
}

static void player_air_accelerate(Player* player, float dt)
{
  player->vertical_velocity -= GRAVITY * dt;
  player_flat_accelerate(player, AIR_MAX_VELOCITY, AIR_ACCELERATION, dt);
}

static void player_ground_accelerate(Player* player, float dt)
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

static void player_accelerate(Player* player, const Input* input, float dt)
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

static void player_steer(Player* player, const Input* input, float dt)
{
  float turn = !!(input->held & KEY_LEFT) - !!(input->held & KEY_RIGHT);
  player->angle += turn * TURN_SPEED * dt;

  E_Vec2f dir = {cosf(player->angle), sinf(player->angle)};
  E_Vec2f right = {dir.y, -dir.x};
  player->camera.dir = dir;

  float forward = !!(input->held & KEY_W) - !!(input->held & KEY_S);
  float strafe = !!(input->held & KEY_D) - !!(input->held & KEY_A);

  E_Vec2f wish = {dir.x * forward + right.x * strafe, dir.y * forward + right.y * strafe};
  float len = e_vec2f_mag(wish);

  if (len > 0.0f) player->wish_direction = (E_Vec2f){wish.x / len, wish.y / len};
  else player->wish_direction = (E_Vec2f){0.0f, 0.0f};

  player_accelerate(player, input, dt);

  player->angle -= input->mouse_dx * MOUSE_SENSITIVITY;
  player->camera.pitch -= input->mouse_dy * MOUSE_SENSITIVITY;
  if (player->camera.pitch > MAX_PITCH) player->camera.pitch = MAX_PITCH;
  if (player->camera.pitch < -MAX_PITCH) player->camera.pitch = -MAX_PITCH;
}

static void player_move(Player* player, const E_Map* map, float dt)
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
    player->velocity = (E_Vec2f){0.0f, 0.0f};
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

int player_create(Player* player, const E_Map* map)
{
  memset(player, 0, sizeof(*player));

  E_Vec2f pos = {0.0f, 0.0f};
  float angle = 0.0f;

  player->angle = angle;
  player->radius = PLAYER_RADIUS;
  player->max_speed = MAX_VELOCITY;
  player->camera.pos = pos;
  player->camera.dir = (E_Vec2f){cosf(angle), sinf(angle)};
  player->camera.fov = PLAYER_FOV * (3.14159265f / 180.0f);
  player->camera.sector = get_camera_sector(map, pos);

  if (!player->camera.sector)
  {
    fprintf(stderr, "player_create: spawn point (%.2f, %.2f) is outside the map\n", pos.x, pos.y);
    return -1;
  }

  player->camera.z = player->camera.sector->floor_z + PLAYER_HEIGHT;
  return 0;
}

void player_update(Player* player, const Input* input, const E_Map* map, float dt)
{
  player_steer(player, input, dt);
  player_move(player, map, dt);
}
