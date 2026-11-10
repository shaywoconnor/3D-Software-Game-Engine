#include <stdio.h>
#include <string.h>

#include "app.h"
#include "engine.h"

int app_init(App* app, const char* title, int width, int height, int scale, int vsync)
{
  memset(app, 0, sizeof(*app));

  if (width <= 0 || height <= 0 || scale <= 0)
  {
    fprintf(stderr, "app_init: invalid size %dx%d at scale %d\n", width, height, scale);
    return -1;
  }

  if (platform_init(&app->platform, title, width, height, scale, vsync) != 0) return -1;

  e_init();

  if (render_pool_init(&app->pool) != 0)
  {
    platform_shutdown(&app->platform);
    return -1;
  }

  return 0;
}

void app_shutdown(App* app)
{
  render_pool_shutdown(&app->pool);
  platform_shutdown(&app->platform);
}
