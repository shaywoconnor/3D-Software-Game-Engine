#ifndef ENGINE_H
#define ENGINE_H


#include <stdlib.h>
#include <stdio.h>
#include "e_map.h"
#include "e_math.h"

void e_render
(
  const E_Map* map,
  E_Vec2f pos,
  E_Vec2f dir,
  float camera_z,
  float fov,
  int x_s,
  int x_e,
  E_Map_Sector* camera_sector,
  uint32_t* framebuffer,
  int buf_width,
  int buf_height,
  float pitch
);

E_Map_Sector* get_camera_sector
(
  const E_Map* map,
  const E_Vec2f pos
);

typedef void (*E_Debug_Hook)(void* user);
extern E_Debug_Hook e_debug_hook;
extern void* e_debug_user;
int e_init(E_Map* map);
#endif
