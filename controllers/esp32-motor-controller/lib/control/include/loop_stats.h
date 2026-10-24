#ifndef LOOP_STATS_H
#define LOOP_STATS_H

#include <stdint.h>

/* Running min / max / mean over uint32_t samples (e.g. CPU cycles). */
typedef struct {
    uint32_t min, max, count;
    uint64_t sum;
} loop_stats_t;

void loop_stats_reset(loop_stats_t *s);
void loop_stats_add(loop_stats_t *s, uint32_t v);
uint32_t loop_stats_mean(const loop_stats_t *s); /* 0 when empty */

#endif
