#include <SDL3/SDL.h>

#include "input.h"

struct Key_Binding
{
  SDL_Scancode scancode;
  uint32_t key;
};
typedef struct Key_Binding Key_Binding;

static const Key_Binding bindings[] =
{
  {SDL_SCANCODE_ESCAPE, KEY_ESCAPE},
  {SDL_SCANCODE_W, KEY_W},
  {SDL_SCANCODE_A, KEY_A},
  {SDL_SCANCODE_S, KEY_S},
  {SDL_SCANCODE_D, KEY_D},
  {SDL_SCANCODE_Q, KEY_Q},
  {SDL_SCANCODE_E, KEY_E},
  {SDL_SCANCODE_R, KEY_R},
  {SDL_SCANCODE_F, KEY_F},
  {SDL_SCANCODE_UP, KEY_UP},
  {SDL_SCANCODE_DOWN, KEY_DOWN},
  {SDL_SCANCODE_LEFT, KEY_LEFT},
  {SDL_SCANCODE_RIGHT, KEY_RIGHT},
  {SDL_SCANCODE_SPACE, KEY_SPACE},
  {SDL_SCANCODE_LCTRL, KEY_CTRL},
  {SDL_SCANCODE_RCTRL, KEY_CTRL},
  {SDL_SCANCODE_LSHIFT, KEY_SHIFT},
  {SDL_SCANCODE_RSHIFT, KEY_SHIFT},
  {SDL_SCANCODE_TAB, KEY_TAB},
  {SDL_SCANCODE_RETURN, KEY_ENTER},
  {SDL_SCANCODE_1, KEY_1},
  {SDL_SCANCODE_2, KEY_2},
  {SDL_SCANCODE_3, KEY_3},
  {SDL_SCANCODE_4, KEY_4},
  {SDL_SCANCODE_5, KEY_5},
  {SDL_SCANCODE_F1, KEY_F1},
  {SDL_SCANCODE_F2, KEY_F2},
  {SDL_SCANCODE_F3, KEY_F3},
  {SDL_SCANCODE_F4, KEY_F4}
};

void input_update(Input* input)
{
  uint32_t held = 0;
  SDL_Event event;
  input->mouse_dx = 0.0f;
  input->mouse_dy = 0.0f;

  while (SDL_PollEvent(&event))
  {
    if (event.type == SDL_EVENT_QUIT) held |= KEY_QUIT;
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
      input->mouse_dx += event.motion.xrel;
      input->mouse_dy += event.motion.yrel;
    }
  }

  const bool* keys = SDL_GetKeyboardState(NULL);
  for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++)
  {
    if (keys[bindings[i].scancode]) held |= bindings[i].key;
  }

  input->pressed = held & ~input->held;
  input->released = input->held & ~held;
  input->held = held;
}
