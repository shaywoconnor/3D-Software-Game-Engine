#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

enum Key
{
  KEY_QUIT = 1u << 0,
  KEY_ESCAPE = 1u << 1,
  KEY_W = 1u << 2,
  KEY_A = 1u << 3,
  KEY_S = 1u << 4,
  KEY_D = 1u << 5,
  KEY_Q = 1u << 6,
  KEY_E = 1u << 7,
  KEY_R = 1u << 8,
  KEY_F = 1u << 9,
  KEY_UP = 1u << 10,
  KEY_DOWN = 1u << 11,
  KEY_LEFT = 1u << 12,
  KEY_RIGHT = 1u << 13,
  KEY_SPACE = 1u << 14,
  KEY_CTRL = 1u << 15,
  KEY_SHIFT = 1u << 16,
  KEY_TAB = 1u << 17,
  KEY_ENTER = 1u << 18,
  KEY_1 = 1u << 19,
  KEY_2 = 1u << 20,
  KEY_3 = 1u << 21,
  KEY_4 = 1u << 22,
  KEY_5 = 1u << 23,
  KEY_F1 = 1u << 24,
  KEY_F2 = 1u << 25,
  KEY_F3 = 1u << 26,
  KEY_F4 = 1u << 27
};
typedef enum Key Key;

struct Input
{
  uint32_t held;
  uint32_t pressed;
  uint32_t released;
  float mouse_dx;
  float mouse_dy;
};
typedef struct Input Input;

void input_update(Input* input);

#endif
