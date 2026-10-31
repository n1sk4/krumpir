#ifndef TICK_TASK_H
#define TICK_TASK_H

#include <stdint.h>

#include "loop_stats.h"

/* A fixed-rate control task on one core, driven by a hardware timer.
 *
 * Every tick it raises the timing pin, calls the body, feeds the task watchdog and lowers the pin,
 * so the pin's period is the loop period and its pulse width the body's execution time. It
 * records the period and execution time (in CPU cycles) and the number of missed ticks.
 *
 * Test aids, set with -D in the build flags:
 *   CONTROL_BUSY_US=200        busy-wait that many microseconds inside every tick
 *   CONTROL_HANG_AT_TICK=3000  spin forever at that tick, to check the watchdog resets the board */

typedef struct {
  uint32_t hz;             /* tick rate */
  int timing_gpio;         /* output pin raised while the body runs (0..31) */
  int core;                /* core the task and its timer interrupt run on */
  uint32_t stack;          /* task stack size in bytes */
  uint32_t wdt_feed_ticks; /* feed the task watchdog every this many ticks */
} tick_cfg_t;

#define TICK_CFG_DEFAULT {1000, 23, 1, 4096, 100}

typedef void (*tick_init_t)(void *ctx); /* runs once on the task, before the first tick */
typedef void (*tick_body_t)(void *ctx, uint32_t tick);

/* Starts the task. init may be NULL. The body must not block. */
void tick_task_start(const tick_cfg_t *cfg, tick_init_t init, tick_body_t body, void *ctx);

/* Snapshot of period / exec-time stats (in CPU cycles) and missed ticks, then resets them. */
void tick_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns);

#endif
