#include "control_task.h"

#include <math.h>

#include "t12_io_sim.h"
#include "t12_loop.h"
#include "telemetry.h"
#include "tick_task.h"

#include "osal.h"

#ifdef KRUMPIR_XCP
#include <string.h>

#include "xcplib.h"

/* XCP calibration page: setpoint and gains, writable from an XCP tool while the loop runs. XCPlite
 * needs the type and the variable to have the same name. */
struct t12_cal_page {
  float setpoint; /* [degC] */
  float kp, ki, kd;
};
const struct t12_cal_page t12_cal_page = {350.0f, 0.05f, 0.05f, 0.0f};
CalSegDecl(t12_cal_page);
XCP_UNIT(t12_cal_page__setpoint, "degC");
XCP_LIMITS(t12_cal_page__setpoint, 100.0f, 450.0f);
XCP_LIMITS(t12_cal_page__kp, 0.0f, 1000.0f);
XCP_LIMITS(t12_cal_page__ki, 0.0f, 1000.0f);
XCP_LIMITS(t12_cal_page__kd, 0.0f, 1000.0f);

static bool gain_ok(float g) { return isfinite(g) && g >= 0.0f && g <= 1000.0f; }

/* A changed calibration page overrides the console settings once; values out of range are ignored.
 * Returns true when it changed p. */
static bool apply_cal_page(ctrl_params_t *p)
{
  static struct t12_cal_page last;
  static bool seen;
  const struct t12_cal_page *c = CalSegLock(t12_cal_page);
  struct t12_cal_page now = *c;
  CalSegUnlock(t12_cal_page);
  bool changed = seen && memcmp(&now, &last, sizeof now) != 0;
  last = now;
  seen = true;
  if (!changed)
    return false;
  if (now.setpoint >= SP_MIN_C && now.setpoint <= SP_MAX_C)
    p->sp = now.setpoint;
  if (gain_ok(now.kp) && gain_ok(now.ki) && gain_ok(now.kd)) {
    p->kp = now.kp;
    p->ki = now.ki;
    p->kd = now.kd;
  }
  return true;
}
#endif

#define TICK_HZ 1000
#define TICK_DT_S (1.0f / TICK_HZ)
#define DEFAULT_AMBIENT_C 25.0

/* Simulation settings handed over from the console. */
typedef struct {
  float load;
  double ambient;
  t12_fault_t fault;
  t12_coeffs_t coeffs; /* valid once coeffs_gen is non-zero */
  uint32_t coeffs_gen;
} sim_cfg_t;

static osal_crit_t s_param_mux = OSAL_CRIT_INIT; /* params and sim settings */
static const t12_ctrl_cfg_t s_ctrl_default = T12_CTRL_CFG_DEFAULT;
static ctrl_params_t s_params = {350.0f, 0, 0, 0, 0, false};
static sim_cfg_t s_sim = {.ambient = DEFAULT_AMBIENT_C};
static volatile float s_temp, s_handle, s_amps, s_duty;
static volatile t12_trip_t s_trip;
static volatile bool s_clear_request;

/* State of the control task: large, so it lives here and not on the task stack. */
static t12_io_sim_t s_sim_dev;
static t12_loop_t s_loop;
static t12_io_t s_io;
static ctrl_params_t s_applied;
static uint32_t s_applied_coeffs_gen;

static void control_init(void *arg)
{
  const t12_params_t plant_params = T12_PARAMS_DEFAULT;
  const t12_safety_cfg_t safety_cfg = T12_SAFETY_CFG_DEFAULT;
  const t12_cal_t cal = T12_CAL_DEFAULT;
  t12_io_sim_init(&s_sim_dev, &s_io, &plant_params, TICK_DT_S, DEFAULT_AMBIENT_C);
  t12_loop_init(&s_loop, &s_ctrl_default, &safety_cfg, &cal, TICK_DT_S);
  s_applied = (ctrl_params_t){0};
  s_applied.sp = s_loop.ctrl.setpoint;
}

static void control_tick(void *arg, uint32_t tick)
{
  /* Take the settings for this tick in one short critical section (no tearing). */
  osal_crit_enter(&s_param_mux);
  ctrl_params_t p = s_params;
  sim_cfg_t sim_cfg = s_sim;
  bool clear = s_clear_request;
  s_clear_request = false;
  osal_crit_exit(&s_param_mux);

#ifdef KRUMPIR_XCP
  DaqCreateEvent(tick);  /* every 1 ms tick */
  DaqCreateEvent(cycle); /* every completed heater cycle (about 10 Hz) */
  if (apply_cal_page(&p)) {
    osal_crit_enter(&s_param_mux);
    s_params.sp = p.sp;
    s_params.kp = p.kp;
    s_params.ki = p.ki;
    s_params.kd = p.kd;
    osal_crit_exit(&s_param_mux);
  }
#endif

  if (sim_cfg.coeffs_gen != s_applied_coeffs_gen) {
    t12_plant_set_coeffs(&s_sim_dev.plant, &sim_cfg.coeffs);
    s_applied_coeffs_gen = sim_cfg.coeffs_gen;
  }
  s_sim_dev.plant.fault = sim_cfg.fault;
  s_sim_dev.ambient = sim_cfg.ambient;

  if (clear)
    t12_loop_clear(&s_loop);
  if (p.kp != s_applied.kp || p.ki != s_applied.ki || p.kd != s_applied.kd)
    t12_ctrl_set_gains(&s_loop.ctrl, p.kp, p.ki, p.kd);
  if (p.sp != s_applied.sp)
    t12_ctrl_set_setpoint(&s_loop.ctrl, p.sp);
  s_loop.ctrl.cfg.ramp = p.ramp;
  bool start_refused = false;
  if (p.run != s_applied.run) {
    if (p.run)
      start_refused = !t12_loop_start(&s_loop); /* refused while a trip is latched */
    else
      t12_loop_stop(&s_loop);
  }
  s_applied = p;

  bool fresh = t12_loop_tick(&s_loop, &s_io);
  t12_io_sim_step(&s_sim_dev);

  /* A trip, or a refused start, drops the run flag again until the user starts the controller. */
  if (p.run && (start_refused || !t12_safety_ok(&s_loop.safety))) {
    osal_crit_enter(&s_param_mux);
    s_params.run = false;
    osal_crit_exit(&s_param_mux);
  }
  s_trip = s_loop.safety.latched;
  if (fresh) {
    s_temp = s_loop.temp;
    s_handle = s_loop.handle;
    s_amps = s_loop.amps;
    s_duty = s_loop.duty;
    telem_sample_t s = {tick, {s_loop.ctrl.sp_active, s_loop.temp, s_loop.duty, s_loop.amps}};
    telemetry_push(&s);
  }
#ifdef KRUMPIR_XCP
  /* Measurable from here: the file-scope state, such as s_loop (controller, safety, last
   * measurement) and s_sim_dev.plant (simulated tip). */
  DaqTriggerEvent(tick);
  if (fresh)
    DaqTriggerEvent(cycle);
#endif
}

void control_task_start(void)
{
  s_params.kp = s_ctrl_default.kp;
  s_params.ki = s_ctrl_default.ki;
  s_params.kd = s_ctrl_default.kd;
  s_params.ramp = s_ctrl_default.ramp;
  tick_cfg_t cfg = TICK_CFG_DEFAULT;
  cfg.hz = TICK_HZ;
  cfg.stack = 6144;
  tick_task_start(&cfg, control_init, control_tick, NULL);
}

void control_task_set_params(const ctrl_params_t *p)
{
  osal_crit_enter(&s_param_mux);
  s_params = *p;
  osal_crit_exit(&s_param_mux);
}

void control_task_get_params(ctrl_params_t *p)
{
  osal_crit_enter(&s_param_mux);
  *p = s_params;
  osal_crit_exit(&s_param_mux);
}

void control_task_set_load(float w_per_k)
{
  /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
  t12_params_t params = T12_PARAMS_DEFAULT;
  params.g_load = w_per_k;
  t12_coeffs_t coeffs;
  t12_plant_coeffs(&params, TICK_DT_S, &coeffs);

  osal_crit_enter(&s_param_mux);
  s_sim.load = w_per_k;
  s_sim.coeffs = coeffs;
  s_sim.coeffs_gen++;
  osal_crit_exit(&s_param_mux);
}

void control_task_set_fault(t12_fault_t fault)
{
  osal_crit_enter(&s_param_mux);
  s_sim.fault = fault;
  osal_crit_exit(&s_param_mux);
}

void control_task_set_ambient(float celsius)
{
  osal_crit_enter(&s_param_mux);
  s_sim.ambient = celsius;
  osal_crit_exit(&s_param_mux);
}

void control_task_get_sim(float *load, t12_fault_t *fault, float *ambient)
{
  osal_crit_enter(&s_param_mux);
  *load = s_sim.load;
  *fault = s_sim.fault;
  *ambient = (float)s_sim.ambient;
  osal_crit_exit(&s_param_mux);
}

t12_trip_t control_task_get_trip(void) { return s_trip; }

void control_task_clear_trip(void) { s_clear_request = true; }

void control_task_get_state(control_state_t *s)
{
  s->temp = s_temp;
  s->handle = s_handle;
  s->amps = s_amps;
  s->duty = s_duty;
}
