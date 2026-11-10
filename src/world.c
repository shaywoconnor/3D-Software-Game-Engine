#include <stdio.h>
#include <string.h>

#include "world.h"

int world_create(World* world, const char* map_path)
{
  memset(world, 0, sizeof(*world));

  if (!map_path || !map_path[0])
  {
    fprintf(stderr, "world_create: no map path given\n");
    return -1;
  }

  if (E_Map_Import(&world->map, map_path, 0) != 0) return -1;

  if (world->map.sector_count < 1)
  {
    fprintf(stderr, "%s: map has no sectors\n", map_path);
    goto fail;
  }

  for (int i = 0; i < world->map.texture_count; i++)
  {
    if (E_Map_Texture_Load(&world->map.textures[i]) != 0) goto fail;
  }

  return 0;

fail:
  world_destroy(world);
  return -1;
}

void world_destroy(World* world)
{
  E_Map_Free(&world->map);
  memset(world, 0, sizeof(*world));
}
