#ifndef STATS_H
#define STATS_H

#include <stdint.h>

struct Frame_Stats
{
  uint64_t window_start;
  int frames;
  double render_ms_total;
  char fps_text[64];
  char render_text[64];
};
typedef struct Frame_Stats Frame_Stats;

void stats_init(Frame_Stats* stats, uint64_t now);
int stats_record(Frame_Stats* stats, uint64_t now, double render_ms);

#endif
