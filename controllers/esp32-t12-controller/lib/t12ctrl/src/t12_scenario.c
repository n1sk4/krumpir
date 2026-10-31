#include "t12_scenario.h"

#include <math.h>

#include "step_metrics.h"
#include "t12_io_sim.h"
#include "t12_loop.h"

#define TICK_DT 1e-3
#define SETPOINT 350.0f
#define AMBIENT 25.0
#define BAND_PCT 2.0f
#define SAMPLE_DT 0.1f

static const char *const NAMES[T12_SCN_COUNT] = {"cold", "load",  "ambient", "open",
                                                  "short", "stuck", "runaway"};

typedef struct {
  double duration, event; /* [s] */
} timing_t;

static const timing_t TIMING[T12_SCN_COUNT] = {
    [T12_SCN_COLD_START] = {30.0, 0.0},    [T12_SCN_LOAD_STEP] = {60.0, 30.0},
    [T12_SCN_AMBIENT_STEP] = {60.0, 30.0}, [T12_SCN_OPEN_TIP] = {20.0, 10.0},
    [T12_SCN_SHORTED_TIP] = {20.0, 10.0},  [T12_SCN_STUCK_SWITCH] = {20.0, 10.0},
    [T12_SCN_RUNAWAY] = {80.0, 10.0},
};

const char *t12_scenario_name(t12_scenario_t id) { return NAMES[id]; }

static void apply_event(t12_scenario_t id, t12_io_sim_t *sim, double *load)
{
  switch (id) {
  case T12_SCN_LOAD_STEP: *load = 0.1; break;
  case T12_SCN_RUNAWAY: *load = 0.5; break;
  case T12_SCN_AMBIENT_STEP: sim->ambient = 10.0; break;
  case T12_SCN_OPEN_TIP: sim->plant.fault = T12_FAULT_OPEN; break;
  case T12_SCN_SHORTED_TIP: sim->plant.fault = T12_FAULT_SHORT; break;
  case T12_SCN_STUCK_SWITCH: sim->plant.fault = T12_FAULT_STUCK_ON; break;
  default: break;
  }
  t12_params_t p = T12_PARAMS_DEFAULT;
  p.g_load = *load;
  t12_plant_set_params(&sim->plant, &p, TICK_DT); /* keeps the state */
}

t12_scn_result_t t12_scenario_run(t12_scenario_t id, const t12_ctrl_cfg_t *ctrl,
                                  t12_scn_sink_t sink, void *ctx)
{
  const t12_params_t plant = T12_PARAMS_DEFAULT;
  const t12_safety_cfg_t safety = T12_SAFETY_CFG_DEFAULT;
  const t12_cal_t cal = T12_CAL_DEFAULT;
  t12_io_sim_t sim;
  t12_io_t io;
  t12_loop_t loop;
  t12_io_sim_init(&sim, &io, &plant, TICK_DT, AMBIENT);
  t12_loop_init(&loop, ctrl, &safety, &cal, (float)TICK_DT);
  t12_ctrl_set_setpoint(&loop.ctrl, SETPOINT);
  t12_loop_start(&loop);

  step_metrics_t m;
  step_metrics_init(&m, SETPOINT, BAND_PCT, SAMPLE_DT);
  t12_scn_result_t res = {.min_temp = 1e9f, .trip = T12_TRIP_NONE, .trip_s = -1.0f};
  const timing_t tm = TIMING[id];
  double load = 0.0;
  bool event_done = tm.event <= 0.0;

  for (long k = 0; k < (long)(tm.duration / TICK_DT); k++) {
    double t = (double)k * TICK_DT;
    if (!event_done && t >= tm.event) {
      apply_event(id, &sim, &load);
      event_done = true;
    }
    if (t12_loop_tick(&loop, &io)) {
      if (sink) {
        t12_scn_sample_t s = {(float)t,
                              loop.ctrl.sp_active,
                              loop.temp,
                              (float)sim.plant.t_t,
                              (float)sim.plant.t_h,
                              (float)sim.plant.t_c,
                              loop.duty,
                              loop.amps,
                              (float)load,
                              (float)sim.ambient,
                              loop.safety.latched};
        sink(ctx, &s);
      }
      if (t >= tm.event && isfinite(loop.temp)) {
        step_metrics_add(&m, loop.temp);
        if (loop.temp < res.min_temp)
          res.min_temp = loop.temp;
      }
      if (loop.safety.latched != T12_TRIP_NONE && res.trip == T12_TRIP_NONE) {
        res.trip = loop.safety.latched;
        res.trip_s = (float)(t - tm.event);
      }
    }
    t12_io_sim_step(&sim);
  }

  res.overshoot_pct = step_metrics_overshoot_pct(&m);
  res.undershoot_pct = step_metrics_undershoot_pct(&m);
  res.settling_s = step_metrics_settling_s(&m);
  res.final_error = step_metrics_final_error(&m);
  return res;
}
