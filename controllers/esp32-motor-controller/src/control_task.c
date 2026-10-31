#include "control_task.h"

#include "io_sim.h"
#include "pid.h"
#include "telemetry.h"
#include "tick_task.h"

#include "freertos/FreeRTOS.h"

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
static ctrl_params_t s_params = CTRL_PARAMS_DEFAULT;
static sim_cfg_t s_sim = {.inertia_scale = 1.0f};
static volatile float s_rpm, s_duty;
static volatile fault_t s_fault;
static volatile bool s_clear_request;

/* State of the control task. */
static io_sim_t s_sim_dev;
static io_t s_io;
static pid_ctrl_t s_pid;
static safety_t s_safety;
static ctrl_params_t s_applied;
static uint32_t s_applied_coeffs_gen;

static void control_init(void *arg)
{
  plant_params_t params = PLANT_PARAMS_N20;
  io_sim_init(&s_sim_dev, &s_io, &params, TICK_DT_S);
  s_applied = (ctrl_params_t)CTRL_PARAMS_DEFAULT;
  pid_init(&s_pid, s_applied.kp, s_applied.ki, s_applied.kd, -1.0f, 1.0f, TICK_DT_S, 0.0f);
  const safety_cfg_t safety_cfg = SAFETY_CFG_DEFAULT;
  safety_init(&s_safety, &safety_cfg);
  s_applied_coeffs_gen = 0;
}

static void control_tick(void *arg, uint32_t tick)
{
  /* Take the settings for this cycle in one short critical section (no tearing). */
  portENTER_CRITICAL(&s_param_mux);
  ctrl_params_t p = s_params;
  sim_cfg_t sim_cfg = s_sim;
  portEXIT_CRITICAL(&s_param_mux);

  if (sim_cfg.coeffs_gen != s_applied_coeffs_gen) {
    plant_sim_set_coeffs(&s_sim_dev.plant, &sim_cfg.coeffs);
    s_applied_coeffs_gen = sim_cfg.coeffs_gen;
  }
  io_sim_set_load(&s_sim_dev, sim_cfg.load_nm);

  if (s_clear_request) {
    s_clear_request = false;
    safety_clear(&s_safety);
  }
  float rpm = s_io.sensor_read(s_io.ctx);
  fault_t fault = safety_update(&s_safety, rpm);
  s_fault = fault;
  if (fault != FAULT_NONE && p.run) {
    p.run = false; /* a fault stops the controller until the user starts it again */
    portENTER_CRITICAL(&s_param_mux);
    s_params.run = false;
    portEXIT_CRITICAL(&s_param_mux);
  }

  if (p.kp != s_applied.kp || p.ki != s_applied.ki || p.kd != s_applied.kd) {
    pid_set_gains(&s_pid, p.kp, p.ki, p.kd);
  }
  if (p.run != s_applied.run)
    pid_reset(&s_pid); /* start and stop both begin from zero */
  s_applied = p;

  float duty = 0.0f;
  if (p.run) {
    duty = pid_update(&s_pid, p.sp, rpm);
    s_io.actuator_write(s_io.ctx, duty);
  } else {
    s_io.safe_state(s_io.ctx); /* motor coasts down in the simulation */
  }
  io_sim_step(&s_sim_dev);
  s_rpm = rpm;
  s_duty = duty;

  if (tick % TELEMETRY_DECIMATION == 0) {
    telem_sample_t s = {tick, {p.sp, rpm, duty}};
    telemetry_push(&s);
  }
}

void control_task_start(void)
{
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

fault_t control_task_get_fault(void) { return s_fault; }

void control_task_clear_fault(void) { s_clear_request = true; }

void control_task_get_state(float *rpm, float *duty)
{
  *rpm = s_rpm;
  *duty = s_duty;
}
