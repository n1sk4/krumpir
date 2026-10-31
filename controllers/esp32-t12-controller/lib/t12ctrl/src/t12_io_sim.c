#include "t12_io_sim.h"

#include <math.h>

#include "t12_hw.h"

static uint16_t to_counts(double volts)
{
  double c = floor(volts / T12_ADC_VREF * T12_ADC_MAX + 0.5);
  return (uint16_t)(c < 0 ? 0 : c > T12_ADC_MAX ? T12_ADC_MAX : c);
}

static void heater_set(void *ctx, bool on) { ((t12_io_sim_t *)ctx)->heater = on; }

static uint16_t tc_read(void *ctx) { return to_counts(((t12_io_sim_t *)ctx)->plant.v_amp); }

static uint16_t ntc_read(void *ctx)
{
  double t_k = ((t12_io_sim_t *)ctx)->plant.t_c + 273.15;
  double r = T12_NTC_R25 * exp(T12_NTC_BETA * (1.0 / t_k - 1.0 / 298.15));
  return to_counts(T12_ADC_VREF * r / (r + T12_NTC_R_FIXED));
}

static uint16_t current_read(void *ctx)
{
  double amps = ((t12_io_sim_t *)ctx)->plant.i_heater;
  return to_counts(amps * T12_SHUNT_OHM * T12_INA_GAIN);
}

static void heartbeat_toggle(void *ctx) { ((t12_io_sim_t *)ctx)->hb_age = 0; }

static void safe_state(void *ctx) { ((t12_io_sim_t *)ctx)->heater = false; }

void t12_io_sim_init(t12_io_sim_t *s, t12_io_t *io_out, const t12_params_t *p, double dt,
                     double ambient)
{
  t12_plant_init(&s->plant, p, dt, ambient);
  s->ambient = ambient;
  s->heater = false;
  s->hb_age = 0;
  s->hb_timeout = (uint32_t)(T12_HEARTBEAT_TIMEOUT_S / dt + 0.5);
  io_out->heater_set = heater_set;
  io_out->tc_read = tc_read;
  io_out->ntc_read = ntc_read;
  io_out->current_read = current_read;
  io_out->heartbeat_toggle = heartbeat_toggle;
  io_out->safe_state = safe_state;
  io_out->ctx = s;
}

void t12_io_sim_step(t12_io_sim_t *s)
{
  s->plant.series_on = s->hb_age <= s->hb_timeout;
  t12_plant_step(&s->plant, s->heater, s->ambient);
  if (s->hb_age <= s->hb_timeout)
    s->hb_age++;
}
