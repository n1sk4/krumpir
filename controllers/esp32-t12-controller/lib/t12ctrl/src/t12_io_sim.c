#include "t12_io_sim.h"

#include "t12_frontend.h"
#include "t12_hw.h"

static void heater_set(void *ctx, bool on) { ((t12_io_sim_t *)ctx)->heater = on; }

static uint16_t tc_read(void *ctx) { return t12_tc_counts(((t12_io_sim_t *)ctx)->plant.v_amp); }

static uint16_t ntc_read(void *ctx) { return t12_ntc_counts(((t12_io_sim_t *)ctx)->plant.t_c); }

static uint16_t current_read(void *ctx)
{
  return t12_current_counts(((t12_io_sim_t *)ctx)->plant.i_heater);
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
