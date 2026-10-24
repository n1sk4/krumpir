#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>

#include "loop_stats.h"

/* Starts the 1 kHz control task on core 1. */
void control_task_start(void);

/* Snapshot of period / exec-time stats (in CPU cycles) and missed ticks, then resets them. */
void control_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns);

/* Latest simulated speed [rpm] and duty (-1..+1), for housekeeping output. */
void control_task_get_state(float *rpm, float *duty);

#endif
