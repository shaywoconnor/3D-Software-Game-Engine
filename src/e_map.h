#ifndef E_MAP_H
#define E_MAP_H

#include <stdint.h>
#include "e_math.h"

typedef E_Vec2f E_Map_Vertex;

struct E_Map_Texture
{
  uint32_t* bitmap;
  int width;
  int height;
  const char* path;
};
typedef struct E_Map_Texture E_Map_Texture;

struct E_Map_Sector
{
  float floor_z;
  float ceiling_z;
  int floor_tex;
  int ceiling_tex;

  int first_wall;
  int wall_count;
};
typedef struct E_Map_Sector E_Map_Sector;

struct E_Map_Wall
{
  int v1, v2;
  int neighbour;
  int tex;
};
typedef struct E_Map_Wall E_Map_Wall;

struct E_Map
{
  int id;
  char* path;

  E_Map_Vertex* vertices;
  E_Map_Wall* walls;
  E_Map_Sector* sectors;
  E_Map_Texture* textures;

  int vertex_count;
  int wall_count;
  int sector_count;
  int texture_count;
};
typedef struct E_Map E_Map;

int E_Map_Import(E_Map* map, const char* map_path, int map_id);
void E_Map_Free(E_Map* map);
int E_Map_Texture_Load(E_Map_Texture* texture);

#endif
