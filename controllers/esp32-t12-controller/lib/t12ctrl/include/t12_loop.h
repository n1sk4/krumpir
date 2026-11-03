#ifndef T12_LOOP_H
#define T12_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "t12_convert.h"
#include "t12_ctrl.h"
#include "t12_cycle.h"
#include "t12_io.h"
#include "t12_safety.h"

/* The whole controller, advanced once per control tick: heater cycle, conversion, safety and
 * temperature control, plus the heartbeat rule. A trip stops the controller, sends duty 0 and
 * stops the heartbeat so the series switch drops. The firmware task and the tests both run this. */
typedef struct {
  t12_cycle_t cycle;
  t12_ctrl_t ctrl;
  t12_safety_t safety;
  t12_cal_t cal;
  float temp;   /* last tip temperature [degC], NaN when invalid or before the first one */
  float handle; /* handle temperature [degC] */
  float amps;   /* heater current while heating in the last cycle [A] */
  float duty;   /* heating duty of the last cycle */
} t12_loop_t;

void t12_loop_init(t12_loop_t *l, const t12_ctrl_cfg_t *ctrl, const t12_safety_cfg_t *safety,
                   const t12_cal_t *cal, float tick_dt_s);

/* Returns true on the tick that completed a measurement (l->temp etc. are new). */
bool t12_loop_tick(t12_loop_t *l, const t12_io_t *io);

bool t12_loop_start(t12_loop_t *l); /* refused (false) while a trip is latched */
void t12_loop_stop(t12_loop_t *l);
void t12_loop_clear(t12_loop_t *l); /* clears a trip */

#endif
