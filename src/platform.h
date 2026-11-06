#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#include <SDL3/SDL.h>

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

int platform_init(Platform* platform, const char* title, int width, int height, int scale, int vsync);
void platform_shutdown(Platform* platform);
void platform_present(Platform* platform, const char* const* lines, int line_count);
uint64_t platform_time_ns(void);
void platform_set_mouse_captured(Platform* platform, int captured);

#endif
