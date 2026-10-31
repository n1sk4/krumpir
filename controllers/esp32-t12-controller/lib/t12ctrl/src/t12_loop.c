#include "t12_loop.h"

#include <math.h>

void t12_loop_init(t12_loop_t *l, const t12_ctrl_cfg_t *ctrl, const t12_safety_cfg_t *safety,
                   const t12_cal_t *cal, float tick_dt_s)
{
  t12_cycle_init(&l->cycle, T12_CYCLE_PERIOD_TICKS, T12_SETTLE_TICKS, T12_SAMPLE_TICKS);
  t12_ctrl_init(&l->ctrl, ctrl, T12_CYCLE_PERIOD_TICKS * tick_dt_s);
  t12_safety_init(&l->safety, safety);
  l->cal = *cal;
  l->temp = NAN;
  l->handle = NAN;
  l->amps = 0.0f;
  l->duty = 0.0f;
  l->cycles = 0;
}

bool t12_loop_tick(t12_loop_t *l, const t12_io_t *io)
{
  t12_meas_t m;
  bool fresh = t12_cycle_tick(&l->cycle, io, &m);
  if (fresh) {
    l->temp = t12_tip_celsius(&l->cal, m.tc, m.ntc);
    l->handle = t12_handle_celsius(m.ntc);
    l->amps = t12_heater_amps(m.current);
    l->duty = m.duty;
    l->cycles++;
    if (t12_safety_update(&l->safety, &m, l->temp) != T12_TRIP_NONE) {
      t12_ctrl_stop(&l->ctrl);
      t12_cycle_set_duty(&l->cycle, 0.0f);
    } else {
      t12_cycle_set_duty(&l->cycle, t12_ctrl_update(&l->ctrl, l->temp));
    }
  }

  /* The heartbeat runs only while healthy; after a trip the heater is also forced off for the
   * rest of the cycle that was already running. */
  if (t12_safety_ok(&l->safety))
    io->heartbeat_toggle(io->ctx);
  else
    io->safe_state(io->ctx);
  return fresh;
}

bool t12_loop_start(t12_loop_t *l)
{
  if (!t12_safety_ok(&l->safety))
    return false;
  t12_ctrl_start(&l->ctrl, l->temp);
  return true;
}

void t12_loop_stop(t12_loop_t *l) { t12_ctrl_stop(&l->ctrl); }

void t12_loop_clear(t12_loop_t *l) { t12_safety_clear(&l->safety); }
