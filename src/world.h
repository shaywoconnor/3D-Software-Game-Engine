#ifndef WORLD_H
#define WORLD_H

#include "e_map.h"

struct World
{
  E_Map map;
  //later:
  //E_Entities entities;
  //E_Dynamic_Lights lights;
  //...
  //...
};
typedef struct World World;

int world_create(World* world, const char* map_path);
void world_destroy(World* world);

#endif // WORLD_H
