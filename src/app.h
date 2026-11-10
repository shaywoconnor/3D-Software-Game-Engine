#ifndef APP_H
#define APP_H

#include "platform.h"
#include "render_pool.h"

#define APP_WINDOW_TITLE "Game"
#define APP_RENDER_WIDTH 752
#define APP_RENDER_HEIGHT 452
#define APP_WINDOW_SCALE 2
#define APP_VSYNC 0

struct App
{
  Platform platform;
  Render_Pool pool;
};
typedef struct App App;

int app_init(App* app, const char* title, int width, int height, int scale, int vsync);
void app_shutdown(App* app);

#endif
