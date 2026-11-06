#include <string.h>

#include "app.h"

int app_init(App* app, const char* map_path, const char* title, int width, int height, int scale, int vsync)
{
  memset(app, 0, sizeof(*app));

  if (E_Map_Import(&app->map, map_path, 0) != 0) return -1;

  if (platform_init(&app->platform, title, width, height, scale, vsync) != 0)
  {
    E_Map_Free(&app->map);
    return -1;
  }

  if (e_init(&app->map) != 0)
  {
    platform_shutdown(&app->platform);
    E_Map_Free(&app->map);
    return -1;
  }

  for (int i = 0; i < app->map.texture_count; i++) E_Map_Texture_Load(&app->map.textures[i]);

  if (render_pool_init(&app->pool, width) != 0)
  {
    platform_shutdown(&app->platform);
    E_Map_Free(&app->map);
    return -1;
  }

  return 0;
}

void app_shutdown(App* app)
{
  render_pool_shutdown(&app->pool);
  platform_shutdown(&app->platform);
  E_Map_Free(&app->map);
}
