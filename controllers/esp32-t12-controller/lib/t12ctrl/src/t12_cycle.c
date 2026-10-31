#include "t12_cycle.h"

void t12_cycle_init(t12_cycle_t *c, uint32_t period, uint32_t settle, uint32_t samples)
{
  c->period = period;
  c->settle = settle;
  c->samples = samples;
  c->max_heat = period - settle - samples; /* needs period > settle + samples, samples > 0 */
  c->tick = 0;
  c->heat = 0;
  c->acc = 0;
  c->n = 0;
  c->current = 0;
  c->duty = 0.0f;
}

void t12_cycle_set_duty(t12_cycle_t *c, float duty)
{
  c->duty = !(duty > 0.0f) ? 0.0f : duty > 1.0f ? 1.0f : duty;
}

bool t12_cycle_tick(t12_cycle_t *c, const t12_io_t *io, t12_meas_t *meas)
{
  if (c->tick == 0) {
    c->heat = (uint32_t)(c->duty * (float)c->max_heat + 0.5f);
    c->acc = 0;
    c->n = 0;
    c->current = 0;
  }

  bool done = false;
  if (c->tick < c->heat) {
    io->heater_set(io->ctx, true);
    if (c->tick + 1 == c->heat)
      c->current = io->current_read(io->ctx); /* sees the previous tick's heating */
  } else {
    io->heater_set(io->ctx, false);
    uint32_t off = c->tick - c->heat;
    if (off >= c->settle && off < c->settle + c->samples) {
      c->acc += io->tc_read(io->ctx);
      c->n++;
    }
    if (off + 1 == c->settle + c->samples) {
      meas->tc = (uint16_t)((c->acc + c->n / 2) / c->n);
      meas->ntc = io->ntc_read(io->ctx);
      meas->current = c->current;
      meas->current_off = io->current_read(io->ctx); /* heater off for settle + samples ticks */
      meas->current_valid = c->heat >= 2;
      meas->heat_ticks = (uint16_t)c->heat;
      meas->duty = (float)c->heat / (float)c->max_heat;
      done = true;
    }
  }

  c->tick = c->tick + 1 == c->period ? 0 : c->tick + 1;
  return done;
}
