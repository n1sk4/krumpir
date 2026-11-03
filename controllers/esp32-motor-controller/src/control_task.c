#include "control_task.h"

#ifdef KRUMPIR_IO_HIL /* the plant is behind the HIL link (src/io_hil.c), not simulated here */
#include "hil_port.h"
#include "io_hil.h"
#endif
#include "motor_io_sim.h"
#include "motor_loop.h"
#include "osal.h"
#include "telemetry.h"
#include "tick_task.h"

#define TICK_HZ 1000
#define TICK_DT_S (1.0f / TICK_HZ)
#define TELEMETRY_DECIMATION 10 /* 1 kHz -> 100 Hz */

/* Generated from controller.toml: the XCP calibration page. */
#include "gen/ctrl_gen_task.inc"

/* Simulation settings handed over from the console. */
typedef struct {
  float load_nm;
  float inertia_scale;
  plant_coeffs_t coeffs; /* valid once coeffs_gen is non-zero */
  uint32_t coeffs_gen;
} sim_cfg_t;

static osal_crit_t s_sim_mux = OSAL_CRIT_INIT;
static sim_cfg_t s_sim = {.inertia_scale = 1.0f};
static volatile float s_speed, s_duty;
static volatile motor_trip_t s_trip;

/* State of the control task. */
static rte_t s_rte;
#ifdef KRUMPIR_IO_HIL
static motor_io_hil_t s_hil_dev;
#endif
static motor_io_sim_t s_sim_dev;
static motor_loop_t s_loop;
static motor_io_t s_io;
static uint32_t s_applied_coeffs_gen;

/* Runtime hooks: the component's side of rte.h, on the tick except trip(). */
static void set_param(void *ctx, int id, float v)
{
  motor_ctrl_t *c = &s_loop.ctrl;
  switch (id) {
  case PARAM_SP:
    motor_ctrl_set_setpoint(c, v);
    break;
  case PARAM_KP:
    motor_ctrl_set_gains(c, v, c->cfg.ki, c->cfg.kd);
    break;
  case PARAM_KI:
    motor_ctrl_set_gains(c, c->cfg.kp, v, c->cfg.kd);
    break;
  case PARAM_KD:
    motor_ctrl_set_gains(c, c->cfg.kp, c->cfg.ki, v);
    break;
  }
}
static bool start(void *ctx) { return motor_loop_start(&s_loop); }
static void stop(void *ctx) { motor_loop_stop(&s_loop); }
static void clear(void *ctx) { motor_loop_clear(&s_loop); }
static const char *trip(void *ctx)
{
  motor_trip_t t = s_trip;
  return t == MOTOR_TRIP_NONE ? NULL : motor_trip_name(t);
}

static const rte_hooks_t s_hooks = {set_param, start, stop, clear, trip, NULL};

static void control_init(void *arg)
{
  const plant_params_t plant_params = PLANT_PARAMS_N20;
  const motor_ctrl_cfg_t ctrl_cfg = MOTOR_CTRL_CFG_DEFAULT;
  const motor_safety_cfg_t safety_cfg = MOTOR_SAFETY_CFG_DEFAULT;
  motor_io_sim_init(&s_sim_dev, &s_io, &plant_params, TICK_DT_S);
#ifdef KRUMPIR_IO_HIL
  motor_io_hil_init(&s_hil_dev, &s_io); /* replaces the simulated motor behind s_io */
#endif
  motor_loop_init(&s_loop, &ctrl_cfg, &safety_cfg, TICK_DT_S);
  s_applied_coeffs_gen = 0;
}

static void control_tick(void *arg, uint32_t tick)
{
#ifdef KRUMPIR_XCP
  DaqCreateEvent(tick); /* every 1 ms tick */
  ctrl_gen_cal_apply(&s_rte);
#endif

  osal_crit_enter(&s_sim_mux);
  sim_cfg_t sim_cfg = s_sim;
  osal_crit_exit(&s_sim_mux);
  if (sim_cfg.coeffs_gen != s_applied_coeffs_gen) {
    plant_sim_set_coeffs(&s_sim_dev.plant, &sim_cfg.coeffs);
    s_applied_coeffs_gen = sim_cfg.coeffs_gen;
  }
  motor_io_sim_set_load(&s_sim_dev, sim_cfg.load_nm);

#ifdef KRUMPIR_IO_HIL
  hil_port_tick_begin(); /* the newest plant values into s_hil_dev.slots */
  if (s_hil_dev.slots.age == HIL_AGE_NONE) {
    return; /* no plant yet: wait for the runner (a board ticks from boot) */
  }
  if (hil_port_bye()) {
    rte_set_run(&s_rte, false); /* the runner is done: stop, as `stop` on the console */
  }
#endif
  rte_tick_begin(&s_rte); /* clear, changed parameters, start/stop */
  motor_loop_tick(&s_loop, &s_io);
#ifndef KRUMPIR_IO_HIL
  motor_io_sim_step(&s_sim_dev);
#endif
  s_trip = s_loop.safety.latched;
  rte_tick_end(&s_rte); /* a trip or a refused start drops the run request */
#ifdef KRUMPIR_IO_HIL
  hil_port_tick_end(); /* the duty out */
#endif

  s_speed = s_loop.speed;
  s_duty = s_loop.duty;
  if (tick % TELEMETRY_DECIMATION == 0) {
    telem_sample_t s = {tick, CTRL_GEN_TELEM_VALUES};
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
  ctrl_gen_rte_init(&s_rte, &s_hooks);
#ifdef KRUMPIR_XCP
  ctrl_gen_cal_init(&s_rte);
#endif
#ifdef KRUMPIR_IO_HIL
  static const char *const plant[] = CTRL_GEN_HIL_PLANT, *const cmd[] = CTRL_GEN_HIL_CMD;
  hil_port_bind(&s_hil_dev.slots, plant, CTRL_GEN_HIL_N_PLANT, cmd, CTRL_GEN_HIL_N_CMD);
#endif
  tick_cfg_t cfg = TICK_CFG_DEFAULT;
  cfg.hz = TICK_HZ;
  tick_task_start(&cfg, control_init, control_tick, NULL);
}

rte_t *control_task_rte(void) { return &s_rte; }

void control_task_set_load(float torque_nm)
{
  osal_crit_enter(&s_sim_mux);
  s_sim.load_nm = torque_nm;
  osal_crit_exit(&s_sim_mux);
}

void control_task_set_inertia_scale(float scale)
{
  /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
  plant_params_t params = PLANT_PARAMS_N20;
  params.J *= scale;
  plant_coeffs_t coeffs;
  plant_sim_coeffs(&params, TICK_DT_S, &coeffs);

  osal_crit_enter(&s_sim_mux);
  s_sim.inertia_scale = scale;
  s_sim.coeffs = coeffs;
  s_sim.coeffs_gen++;
  osal_crit_exit(&s_sim_mux);
}

void control_task_get_sim(float *load_nm, float *inertia_scale)
{
  osal_crit_enter(&s_sim_mux);
  *load_nm = s_sim.load_nm;
  *inertia_scale = s_sim.inertia_scale;
  osal_crit_exit(&s_sim_mux);
}

void control_task_get_state(control_state_t *s)
{
  s->speed = s_speed;
  s->duty = s_duty;
}
