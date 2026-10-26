#include "scenario.h"

#include <stddef.h>

#include "io_sim.h"
#include "pid.h"
#include "step_metrics.h"

#define DT 1e-3f
#define TARGET_RPM 3000.0f
#define SATURATING_RPM 10000.0f
#define LOAD_NM 0.001f
#define BAND_PCT 2.0f

typedef struct {
  io_sim_t sim;
  io_t io;
  pid_ctrl_t pid;
  scn_sink_t sink;
  void *sink_ctx;
  float t;
  float last_duty;
} run_t;

/* Runs the loop for a while at a fixed setpoint and load, feeding metrics if given. */
static void run_phase(run_t *r, float setpoint, float load, float seconds, step_metrics_t *m)
{
  r->sim.load_torque = load;
  int steps = (int)(seconds / DT + 0.5f);
  for (int k = 0; k < steps; k++) {
    float rpm = r->io.sensor_read(r->io.ctx);
    float duty = pid_update(&r->pid, setpoint, rpm);
    r->io.actuator_write(r->io.ctx, duty);
    r->last_duty = r->sim.duty;
    if (m)
      step_metrics_add(m, rpm);
    if (r->sink) {
      scn_sample_t s = {r->t, setpoint, rpm, r->sim.duty, load};
      r->sink(r->sink_ctx, &s);
    }
    io_sim_step(&r->sim);
    r->t += DT;
  }
}

const char *scenario_name(scenario_t id)
{
  static const char *const names[SCN_COUNT] = {"step", "load", "saturation", "inertia"};
  return id < SCN_COUNT ? names[id] : "";
}

scn_result_t scenario_run(scenario_t id, const ctrl_params_t *gains, scn_sink_t sink, void *ctx)
{
  plant_params_t plant = PLANT_PARAMS_N20;
  run_t r = {.sink = sink, .sink_ctx = ctx};
  io_sim_init(&r.sim, &r.io, &plant, DT);
  pid_init(&r.pid, gains->kp, gains->ki, gains->kd, -1.0f, 1.0f, DT, 0.0f);

  scn_result_t out = {0};
  step_metrics_t m;
  step_metrics_init(&m, TARGET_RPM, BAND_PCT, DT);

  switch (id) {
  case SCN_HEAVY_INERTIA:
    plant.J *= 2.0f;
    plant_sim_set_params(&r.sim.plant, &plant, DT);
    /* then the same step as SCN_STEP, on the heavier rotor */
    run_phase(&r, 0.0f, 0.0f, 0.1f, NULL);
    run_phase(&r, TARGET_RPM, 0.0f, 1.0f, &m);
    break;
  case SCN_STEP:
    run_phase(&r, 0.0f, 0.0f, 0.1f, NULL);
    run_phase(&r, TARGET_RPM, 0.0f, 1.0f, &m);
    break;
  case SCN_LOAD_STEP:
    run_phase(&r, TARGET_RPM, 0.0f, 0.8f, NULL);
    run_phase(&r, TARGET_RPM, LOAD_NM, 0.8f, &m);
    break;
  case SCN_SATURATION:
    run_phase(&r, SATURATING_RPM, 0.0f, 0.6f, NULL);
    out.duty_saturated = r.last_duty;
    run_phase(&r, TARGET_RPM, 0.0f, 1.0f, &m);
    break;
  default:
    break;
  }

  out.overshoot_pct = step_metrics_overshoot_pct(&m);
  out.undershoot_pct = step_metrics_undershoot_pct(&m);
  out.settling_s = step_metrics_settling_s(&m);
  out.final_error_rpm = step_metrics_final_error(&m);
  out.min_rpm = m.min;
  return out;
}
