#ifndef PLAYER_H
#define PLAYER_H

#include "e_map.h"
#include "e_math.h"
#include "engine.h"
#include "input.h"

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

int player_create(Player* player, const E_Map* map);

void player_update(Player* player, const Input* input, const E_Map* map, float dt);

#endif
