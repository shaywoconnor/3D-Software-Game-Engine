#include <stdio.h>
#include <stdlib.h>

#include "e_map.h"

static void print_map(const E_Map *map)
{
  printf("Map id %d: %s\n\n", map->id, map->path);

  printf("Vertices (%d)\n", map->vertex_count);
  for (int i = 0; i < map->vertex_count; i++)
    printf("  [%3d] (%g, %g)\n", i, map->vertices[i].x, map->vertices[i].y);

  printf("\nSectors (%d), walls (%d)\n", map->sector_count, map->wall_count);
  for (int s = 0; s < map->sector_count; s++) {
    const E_Map_Sector *sec = &map->sectors[s];

    printf("  [%3d] floor=%g ceiling=%g floor_tex=%d ceiling_tex=%d walls=%d..%d\n",
           s, sec->floor_z, sec->ceiling_z, sec->floor_tex, sec->ceiling_tex,
           sec->first_wall, sec->first_wall + sec->wall_count - 1);

    for (int i = sec->first_wall; i < sec->first_wall + sec->wall_count; i++) {
      const E_Map_Wall *w = &map->walls[i];
      E_Map_Vertex a = map->vertices[w->v1];
      E_Map_Vertex b = map->vertices[w->v2];

      printf("        wall %3d: v%d (%g, %g) -> v%d (%g, %g)  tex=%d  ",
             i, w->v1, a.x, a.y, w->v2, b.x, b.y, w->tex);

      if (w->neighbour == -1)
        printf("solid\n");
      else
        printf("portal to sector %d\n", w->neighbour);
    }
  }

  printf("\nTextures (%d)\n", map->texture_count);
  for (int i = 0; i < map->texture_count; i++)
    printf("  [%3d] \"%s\"\n", i, map->textures[i].path);
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: %s <map file> [map id]\n", argv[0]);
    return 1;
  }

  int id = argc > 2 ? atoi(argv[2]) : 0;
  E_Map map = {0};

  if (E_Map_Import(&map, argv[1], id) != 0)
    return 1;

  print_map(&map);
  E_Map_Free(&map);
  return 0;
}
