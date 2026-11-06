#ifndef APP_H
#define APP_H

#include "e_map.h"
#include "platform.h"
#include "render_pool.h"

struct App
{
  E_Map map;
  Platform platform;
  Render_Pool pool;
};
typedef struct App App;

int app_init(App* app, const char* map_path, const char* title, int width, int height, int scale, int vsync);
void app_shutdown(App* app);

#endif
