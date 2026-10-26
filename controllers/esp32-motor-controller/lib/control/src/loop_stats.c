#include "loop_stats.h"

void loop_stats_reset(loop_stats_t *s)
{
  s->min = UINT32_MAX;
  s->max = 0;
  s->count = 0;
  s->sum = 0;
}

void loop_stats_add(loop_stats_t *s, uint32_t v)
{
  if (v < s->min)
    s->min = v;
  if (v > s->max)
    s->max = v;
  s->count++;
  s->sum += v;
}

uint32_t loop_stats_mean(const loop_stats_t *s)
{
  return s->count ? (uint32_t)(s->sum / s->count) : 0;
}
