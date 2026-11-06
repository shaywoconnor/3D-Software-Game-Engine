#ifndef E_MATH_H
#define E_MATH_H

#include <math.h>
struct E_Vec2f
{
  float x;
  float y;
};
typedef struct E_Vec2f E_Vec2f;

static inline float e_vec2f_dot(E_Vec2f a, E_Vec2f b) { return a.x * b.x + a.y * b.y; }
static inline float e_vec2f_mag(E_Vec2f a) { return sqrtf(a.x * a.x + a.y * a.y); }

#endif
