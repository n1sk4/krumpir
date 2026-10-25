#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>

#include "ctrl_params.h"
#include "loop_stats.h"

/* Starts the 1 kHz control task on core 1. */
void control_task_start(void);

/* Settings are copied by the control task once per cycle, so a change never tears. */
void control_task_set_params(const ctrl_params_t *p);
void control_task_get_params(ctrl_params_t *p);

/* Snapshot of period / exec-time stats (in CPU cycles) and missed ticks, then resets them. */
void control_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns);

/* Latest simulated speed [rpm] and duty (-1..+1). */
void control_task_get_state(float *rpm, float *duty);

#endif
