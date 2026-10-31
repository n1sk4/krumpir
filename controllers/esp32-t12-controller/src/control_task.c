#include "control_task.h"

#include <math.h>

#include "t12_io_sim.h"
#include "t12_loop.h"
#include "telemetry.h"
#include "tick_task.h"

#include "freertos/FreeRTOS.h"

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

static portMUX_TYPE s_param_mux = portMUX_INITIALIZER_UNLOCKED; /* params and sim settings */
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
  portENTER_CRITICAL(&s_param_mux);
  ctrl_params_t p = s_params;
  sim_cfg_t sim_cfg = s_sim;
  bool clear = s_clear_request;
  s_clear_request = false;
  portEXIT_CRITICAL(&s_param_mux);

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
    portENTER_CRITICAL(&s_param_mux);
    s_params.run = false;
    portEXIT_CRITICAL(&s_param_mux);
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

void control_task_set_load(float w_per_k)
{
  /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
  t12_params_t params = T12_PARAMS_DEFAULT;
  params.g_load = w_per_k;
  t12_coeffs_t coeffs;
  t12_plant_coeffs(&params, TICK_DT_S, &coeffs);

  portENTER_CRITICAL(&s_param_mux);
  s_sim.load = w_per_k;
  s_sim.coeffs = coeffs;
  s_sim.coeffs_gen++;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_fault(t12_fault_t fault)
{
  portENTER_CRITICAL(&s_param_mux);
  s_sim.fault = fault;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_ambient(float celsius)
{
  portENTER_CRITICAL(&s_param_mux);
  s_sim.ambient = celsius;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_get_sim(float *load, t12_fault_t *fault, float *ambient)
{
  portENTER_CRITICAL(&s_param_mux);
  *load = s_sim.load;
  *fault = s_sim.fault;
  *ambient = (float)s_sim.ambient;
  portEXIT_CRITICAL(&s_param_mux);
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
