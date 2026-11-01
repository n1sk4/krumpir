#include "control_task.h"

#include "motor_io_sim.h"
#include "motor_loop.h"
#include "telemetry.h"
#include "tick_task.h"

#include "freertos/FreeRTOS.h"

#ifdef KRUMPIR_XCP
#include <math.h>
#include <string.h>

#include "xcplib.h"

/* XCP calibration page: setpoint and gains, writable from an XCP tool while the loop runs. XCPlite
 * needs the type and the variable to have the same name. */
struct motor_cal_page {
  float setpoint; /* [rpm] */
  float kp, ki, kd;
};
const struct motor_cal_page motor_cal_page = {0.0f, 7.2e-4f, 0.03f, 0.0f};
CalSegDecl(motor_cal_page);
XCP_UNIT(motor_cal_page__setpoint, "rpm");
XCP_LIMITS(motor_cal_page__setpoint, -SP_MAX_RPM, SP_MAX_RPM);
XCP_LIMITS(motor_cal_page__kp, 0.0f, GAIN_MAX);
XCP_LIMITS(motor_cal_page__ki, 0.0f, GAIN_MAX);
XCP_LIMITS(motor_cal_page__kd, 0.0f, GAIN_MAX);

static bool gain_ok(float g) { return isfinite(g) && g >= 0.0f && g <= GAIN_MAX; }

/* A changed calibration page overrides the console settings once; values out of range are ignored.
 * Returns true when it changed p. */
static bool apply_cal_page(ctrl_params_t *p)
{
  static struct motor_cal_page last;
  static bool seen;
  const struct motor_cal_page *c = CalSegLock(motor_cal_page);
  struct motor_cal_page now = *c;
  CalSegUnlock(motor_cal_page);
  bool changed = seen && memcmp(&now, &last, sizeof now) != 0;
  last = now;
  seen = true;
  if (!changed)
    return false;
  if (isfinite(now.setpoint) && fabsf(now.setpoint) <= SP_MAX_RPM)
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
#define TELEMETRY_DECIMATION 10 /* 1 kHz -> 100 Hz */

/* Simulation settings handed over from the console. */
typedef struct {
  float load_nm;
  float inertia_scale;
  plant_coeffs_t coeffs; /* valid once coeffs_gen is non-zero */
  uint32_t coeffs_gen;
} sim_cfg_t;

static portMUX_TYPE s_param_mux = portMUX_INITIALIZER_UNLOCKED; /* params and sim settings */
static const motor_ctrl_cfg_t s_ctrl_default = MOTOR_CTRL_CFG_DEFAULT;
static ctrl_params_t s_params;
static sim_cfg_t s_sim = {.inertia_scale = 1.0f};
static volatile float s_speed, s_duty;
static volatile motor_trip_t s_trip;
static volatile bool s_clear_request;

/* State of the control task. */
static motor_io_sim_t s_sim_dev;
static motor_loop_t s_loop;
static motor_io_t s_io;
static ctrl_params_t s_applied;
static uint32_t s_applied_coeffs_gen;

static void control_init(void *arg)
{
  const plant_params_t plant_params = PLANT_PARAMS_N20;
  const motor_safety_cfg_t safety_cfg = MOTOR_SAFETY_CFG_DEFAULT;
  motor_io_sim_init(&s_sim_dev, &s_io, &plant_params, TICK_DT_S);
  motor_loop_init(&s_loop, &s_ctrl_default, &safety_cfg, TICK_DT_S);
  s_applied = (ctrl_params_t){0, s_ctrl_default.kp, s_ctrl_default.ki, s_ctrl_default.kd, false};
  s_applied_coeffs_gen = 0;
}

static void control_tick(void *arg, uint32_t tick)
{
  /* Take the settings for this tick in one short critical section (no tearing). */
  portENTER_CRITICAL(&s_param_mux);
  ctrl_params_t p = s_params;
  sim_cfg_t sim_cfg = s_sim;
  bool clear = s_clear_request;
  s_clear_request = false;
  portEXIT_CRITICAL(&s_param_mux);

#ifdef KRUMPIR_XCP
  DaqCreateEvent(tick); /* every 1 ms tick */
  if (apply_cal_page(&p)) {
    portENTER_CRITICAL(&s_param_mux);
    s_params.sp = p.sp;
    s_params.kp = p.kp;
    s_params.ki = p.ki;
    s_params.kd = p.kd;
    portEXIT_CRITICAL(&s_param_mux);
  }
#endif

  if (sim_cfg.coeffs_gen != s_applied_coeffs_gen) {
    plant_sim_set_coeffs(&s_sim_dev.plant, &sim_cfg.coeffs);
    s_applied_coeffs_gen = sim_cfg.coeffs_gen;
  }
  motor_io_sim_set_load(&s_sim_dev, sim_cfg.load_nm);

  if (clear)
    motor_loop_clear(&s_loop);
  if (p.kp != s_applied.kp || p.ki != s_applied.ki || p.kd != s_applied.kd)
    motor_ctrl_set_gains(&s_loop.ctrl, p.kp, p.ki, p.kd);
  motor_ctrl_set_setpoint(&s_loop.ctrl, p.sp);
  bool start_refused = false;
  if (p.run != s_applied.run) {
    if (p.run)
      start_refused = !motor_loop_start(&s_loop); /* refused while a trip is latched */
    else
      motor_loop_stop(&s_loop);
  }
  s_applied = p;

  motor_loop_tick(&s_loop, &s_io);
  motor_io_sim_step(&s_sim_dev);

  /* A trip, or a refused start, drops the run flag again until the user starts the controller. */
  if (p.run && (start_refused || !motor_safety_ok(&s_loop.safety))) {
    portENTER_CRITICAL(&s_param_mux);
    s_params.run = false;
    portEXIT_CRITICAL(&s_param_mux);
  }
  s_trip = s_loop.safety.latched;
  s_speed = s_loop.speed;
  s_duty = s_loop.duty;

  if (tick % TELEMETRY_DECIMATION == 0) {
    telem_sample_t s = {tick, {s_loop.ctrl.setpoint, s_loop.speed, s_loop.duty}};
    telemetry_push(&s);
  }
#ifdef KRUMPIR_XCP
  /* Measurable from here: the file-scope state, such as s_loop (controller, safety, last speed and
   * duty) and s_sim_dev.plant (simulated motor). */
  DaqTriggerEvent(tick);
#endif
}

void control_task_start(void)
{
  s_params = (ctrl_params_t){0, s_ctrl_default.kp, s_ctrl_default.ki, s_ctrl_default.kd, false};
  tick_cfg_t cfg = TICK_CFG_DEFAULT;
  cfg.hz = TICK_HZ;
  tick_task_start(&cfg, control_init, control_tick, NULL);
}

void control_task_set_params(const ctrl_params_t *p)
{
  portENTER_CRITICAL(&s_param_mux);
  s_params = *p;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_get_params(ctrl_params_t *p)
{
  portENTER_CRITICAL(&s_param_mux);
  *p = s_params;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_load(float torque_nm)
{
  portENTER_CRITICAL(&s_param_mux);
  s_sim.load_nm = torque_nm;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_inertia_scale(float scale)
{
  /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
  plant_params_t params = PLANT_PARAMS_N20;
  params.J *= scale;
  plant_coeffs_t coeffs;
  plant_sim_coeffs(&params, TICK_DT_S, &coeffs);

  portENTER_CRITICAL(&s_param_mux);
  s_sim.inertia_scale = scale;
  s_sim.coeffs = coeffs;
  s_sim.coeffs_gen++;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_get_sim(float *load_nm, float *inertia_scale)
{
  portENTER_CRITICAL(&s_param_mux);
  *load_nm = s_sim.load_nm;
  *inertia_scale = s_sim.inertia_scale;
  portEXIT_CRITICAL(&s_param_mux);
}

motor_trip_t control_task_get_trip(void) { return s_trip; }

void control_task_clear_trip(void) { s_clear_request = true; }

void control_task_get_state(control_state_t *s)
{
  s->speed = s_speed;
  s->duty = s_duty;
}
