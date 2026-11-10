#include <stdio.h>

#include "app.h"
#include "game.h"

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s <map file>\n", argv[0]);
    return 1;
  }

  App app;
  if (app_init(&app, APP_WINDOW_TITLE, APP_RENDER_WIDTH, APP_RENDER_HEIGHT, APP_WINDOW_SCALE, APP_VSYNC) != 0) return 1;

  int result = game_play(&app, argv[1]);

  app_shutdown(&app);
  return result == 0 ? 0 : 1;
}
