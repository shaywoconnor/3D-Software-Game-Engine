#include <stdio.h>
#include <string.h>

#include "stats.h"

#define STATS_INTERVAL_NS 500000000ull

void stats_init(Frame_Stats* stats, uint64_t now)
{
  memset(stats, 0, sizeof(*stats));
  stats->window_start = now;
  snprintf(stats->fps_text, sizeof(stats->fps_text), "FPS: --");
  snprintf(stats->render_text, sizeof(stats->render_text), "e_render: --");
}

int stats_record(Frame_Stats* stats, uint64_t now, double render_ms)
{
  stats->frames++;
  stats->render_ms_total += render_ms;

  uint64_t elapsed = now - stats->window_start;
  if (elapsed < STATS_INTERVAL_NS) return 0;

  double seconds = elapsed / 1e9;
  snprintf(stats->fps_text, sizeof(stats->fps_text), "FPS: %.0f  (%.2f ms)", stats->frames / seconds, seconds * 1000.0 / stats->frames);
  snprintf(stats->render_text, sizeof(stats->render_text), "e_render: %.2f ms", stats->render_ms_total / stats->frames);

  stats->window_start = now;
  stats->frames = 0;
  stats->render_ms_total = 0.0;
  return 1;
}
