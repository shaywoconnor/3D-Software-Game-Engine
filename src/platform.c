#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

void platform_shutdown(Platform* platform)
{
  free(platform->framebuffer);
  if (platform->texture) SDL_DestroyTexture(platform->texture);
  if (platform->renderer) SDL_DestroyRenderer(platform->renderer);
  if (platform->window) SDL_DestroyWindow(platform->window);
  SDL_Quit();
  memset(platform, 0, sizeof(*platform));
}

int platform_init(Platform* platform, const char* title, int width, int height, int scale, int vsync)
{
  memset(platform, 0, sizeof(*platform));
  platform->width = width;
  platform->height = height;

  if (!SDL_Init(SDL_INIT_VIDEO))
  {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    goto fail;
  }

  platform->window = SDL_CreateWindow(title, width * scale, height * scale, 0);
  if (!platform->window)
  {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    goto fail;
  }
  SDL_SetWindowRelativeMouseMode(platform->window, true);
  platform->mouse_captured = true;

  platform->renderer = SDL_CreateRenderer(platform->window, NULL);
  if (!platform->renderer)
  {
    fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
    goto fail;
  }
  SDL_SetRenderVSync(platform->renderer, vsync);

  platform->texture = SDL_CreateTexture(platform->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!platform->texture)
  {
    fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
    goto fail;
  }
  SDL_SetTextureScaleMode(platform->texture, SDL_SCALEMODE_NEAREST);

  platform->framebuffer = malloc((size_t)width * height * sizeof(uint32_t));
  if (!platform->framebuffer)
  {
    fprintf(stderr, "out of memory allocating framebuffer\n");
    goto fail;
  }

  return 0;

fail:
  platform_shutdown(platform);
  return -1;
}

static void draw_text_shadowed(SDL_Renderer* renderer, float x, float y, const char* text)
{
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderDebugText(renderer, x + 1, y + 1, text);
  SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
  SDL_RenderDebugText(renderer, x, y, text);
}

void platform_present(Platform* platform, const char* const* lines, int line_count)
{
  SDL_UpdateTexture(platform->texture, NULL, platform->framebuffer, platform->width * sizeof(uint32_t));
  SDL_RenderClear(platform->renderer);
  SDL_RenderTexture(platform->renderer, platform->texture, NULL, NULL);

  if (line_count > 0)
  {
    SDL_SetRenderScale(platform->renderer, 2.0f, 2.0f);
    for (int i = 0; i < line_count; i++) draw_text_shadowed(platform->renderer, 4, 4 + 12 * i, lines[i]);
    SDL_SetRenderScale(platform->renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 255);
  }

  SDL_RenderPresent(platform->renderer);
}

void platform_set_mouse_captured(Platform* platform, bool captured)
{
  if (SDL_SetWindowRelativeMouseMode(platform->window, captured)) platform->mouse_captured = captured;
}

void platform_toggle_mouse_captured(Platform* platform)
{
  platform_set_mouse_captured(platform, !platform->mouse_captured);
}

#define PLATFORM_DEBUG_DELAY_MS 4

void platform_debug_present(void* platform)
{
  SDL_PumpEvents();
  platform_present(platform, NULL, 0);
  SDL_Delay(PLATFORM_DEBUG_DELAY_MS);
}

uint64_t platform_time_ns(void)
{
  return SDL_GetTicksNS();
}
