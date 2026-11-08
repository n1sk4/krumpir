#include "t12_cycle.h"

#include "t12_hw.h"

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
  c->tc_bad = false;
  c->lost = false;
  c->current = 0;
  c->duty = 0.0f;
}

/* A reading, noting a missing one (T12_ADC_NONE) for the cycle's measurement. */
static uint16_t note(t12_cycle_t *c, uint16_t v)
{
  if (v == T12_ADC_NONE)
    c->lost = true;
  return v;
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
    c->tc_bad = false;
    c->lost = false;
    c->current = 0;
  }

  bool done = false;
  if (c->tick < c->heat) {
    io->heater_set(io->ctx, true);
    if (c->tick + 1 == c->heat)
      c->current = note(c, io->current_read(io->ctx)); /* sees the previous tick's heating */
  } else {
    io->heater_set(io->ctx, false);
    uint32_t off = c->tick - c->heat;
    if (off >= c->settle && off < c->settle + c->samples) {
      uint16_t tc = note(c, io->tc_read(io->ctx));
      c->tc_bad |= tc >= T12_ADC_MAX;
      c->acc += tc;
      c->n++;
    }
    if (off + 1 == c->settle + c->samples) {
      meas->tc = c->tc_bad ? T12_ADC_MAX : (uint16_t)((c->acc + c->n / 2) / c->n);
      meas->ntc = note(c, io->ntc_read(io->ctx));
      meas->current = c->current;
      /* heater off for settle + samples ticks */
      meas->current_off = note(c, io->current_read(io->ctx));
      meas->current_valid = c->heat >= 2;
      meas->lost = c->lost;
      meas->heat_ticks = (uint16_t)c->heat;
      meas->duty = (float)c->heat / (float)c->max_heat;
      done = true;
    }
  }

  c->tick = c->tick + 1 == c->period ? 0 : c->tick + 1;
  return done;
}
