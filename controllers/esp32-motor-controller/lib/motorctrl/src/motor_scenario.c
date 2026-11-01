#include "motor_scenario.h"

#include <stddef.h>

#include "motor_io_sim.h"
#include "motor_loop.h"
#include "step_metrics.h"

#define DT 1e-3f
#define TARGET_RPM 3000.0f
#define SATURATING_RPM 10000.0f
#define LOAD_NM 0.001f
#define BAND_PCT 2.0f

typedef struct {
  motor_io_sim_t sim;
  motor_io_t io;
  motor_loop_t loop;
  motor_scn_sink_t sink;
  void *sink_ctx;
  float t;
  float last_duty;
} run_t;

/* Runs the loop for a while at a fixed setpoint and load, feeding metrics if given. */
static void run_phase(run_t *r, float setpoint, float load, float seconds, step_metrics_t *m)
{
  r->sim.load_torque = load;
  motor_ctrl_set_setpoint(&r->loop.ctrl, setpoint);
  int steps = (int)(seconds / DT + 0.5f);
  for (int k = 0; k < steps; k++) {
    motor_loop_tick(&r->loop, &r->io);
    float rpm = r->loop.speed;
    r->last_duty = r->sim.duty;
    if (m)
      step_metrics_add(m, rpm);
    if (r->sink) {
      motor_scn_sample_t s = {r->t, setpoint, rpm, r->sim.duty, load};
      r->sink(r->sink_ctx, &s);
    }
    motor_io_sim_step(&r->sim);
    r->t += DT;
  }
}

const char *motor_scenario_name(motor_scenario_t id)
{
  static const char *const names[MOTOR_SCN_COUNT] = {"step", "load", "saturation", "inertia"};
  return id < MOTOR_SCN_COUNT ? names[id] : "";
}

motor_scn_result_t motor_scenario_run(motor_scenario_t id, const motor_ctrl_cfg_t *ctrl,
                                      motor_scn_sink_t sink, void *ctx)
{
  plant_params_t plant = PLANT_PARAMS_N20;
  const motor_safety_cfg_t safety = MOTOR_SAFETY_CFG_DEFAULT;
  run_t r = {.sink = sink, .sink_ctx = ctx};
  motor_io_sim_init(&r.sim, &r.io, &plant, DT);
  motor_loop_init(&r.loop, ctrl, &safety, DT);
  motor_loop_start(&r.loop);

  motor_scn_result_t out = {0};
  step_metrics_t m;
  step_metrics_init(&m, TARGET_RPM, BAND_PCT, DT);

  switch (id) {
  case MOTOR_SCN_HEAVY_INERTIA:
    plant.J *= 2.0f;
    plant_sim_set_params(&r.sim.plant, &plant, DT);
    /* then the same step as MOTOR_SCN_STEP, on the heavier rotor */
    run_phase(&r, 0.0f, 0.0f, 0.1f, NULL);
    run_phase(&r, TARGET_RPM, 0.0f, 1.0f, &m);
    break;
  case MOTOR_SCN_STEP:
    run_phase(&r, 0.0f, 0.0f, 0.1f, NULL);
    run_phase(&r, TARGET_RPM, 0.0f, 1.0f, &m);
    break;
  case MOTOR_SCN_LOAD_STEP:
    run_phase(&r, TARGET_RPM, 0.0f, 0.8f, NULL);
    run_phase(&r, TARGET_RPM, LOAD_NM, 0.8f, &m);
    break;
  case MOTOR_SCN_SATURATION:
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
