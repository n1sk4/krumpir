#include "control_task.h"

#ifdef KRUMPIR_IO_HIL /* the plant is behind the HIL link (src/io_hil.c), not simulated here */
#include "hil_port.h"
#include "io_hil.h"
#endif
#include "osal.h"
#include "t12_io_sim.h"
#include "t12_loop.h"
#include "telemetry.h"
#include "tick_task.h"

#define TICK_HZ 1000
#define TICK_DT_S (1.0f / TICK_HZ)
#define DEFAULT_AMBIENT_C 25.0
#define CONTROL_STACK_BYTES 6144 /* the on-chip heater plant runs in double precision on this task */

/* Generated from controller.toml: the XCP calibration page. */
#include "gen/ctrl_gen_task.inc"

/* Simulation settings handed over from the console. */
typedef struct {
  float load;
  double ambient;
  t12_fault_t fault;
  t12_coeffs_t coeffs; /* valid once coeffs_gen is non-zero */
  uint32_t coeffs_gen;
} sim_cfg_t;

static osal_crit_t s_sim_mux = OSAL_CRIT_INIT;
static const t12_ctrl_cfg_t s_ctrl_default = T12_CTRL_CFG_DEFAULT;
static sim_cfg_t s_sim = {.ambient = DEFAULT_AMBIENT_C};
static volatile float s_temp, s_handle, s_amps, s_duty;
static volatile t12_trip_t s_trip;

/* State of the control task: large, so it lives here and not on the task stack. */
static rte_t s_rte;
#ifdef KRUMPIR_IO_HIL
static t12_io_hil_t s_hil_dev;
#endif
static t12_io_sim_t s_sim_dev;
static t12_loop_t s_loop;
static t12_io_t s_io;
static uint32_t s_applied_coeffs_gen;

/* Runtime hooks: the component's side of rte.h, on the tick except trip(). */
static void set_param(void *ctx, int id, float v)
{
  t12_ctrl_t *c = &s_loop.ctrl;
  switch (id) {
  case PARAM_SP:
    t12_ctrl_set_setpoint(c, v);
    break;
  case PARAM_KP:
    t12_ctrl_set_gains(c, v, c->cfg.ki, c->cfg.kd);
    break;
  case PARAM_KI:
    t12_ctrl_set_gains(c, c->cfg.kp, v, c->cfg.kd);
    break;
  case PARAM_KD:
    t12_ctrl_set_gains(c, c->cfg.kp, c->cfg.ki, v);
    break;
  case PARAM_RAMP:
    c->cfg.ramp = v;
    break;
  }
}
static bool start(void *ctx) { return t12_loop_start(&s_loop); } /* refused while tripped */
static void stop(void *ctx) { t12_loop_stop(&s_loop); }
static void clear(void *ctx) { t12_loop_clear(&s_loop); }
static const char *trip(void *ctx)
{
  t12_trip_t t = s_trip;
  return t == T12_TRIP_NONE ? NULL : t12_trip_name(t);
}

static const rte_hooks_t s_hooks = {set_param, start, stop, clear, trip, NULL};

static void control_init(void *arg)
{
  const t12_params_t plant_params = T12_PARAMS_DEFAULT;
  const t12_safety_cfg_t safety_cfg = T12_SAFETY_CFG_DEFAULT;
  const t12_cal_t cal = T12_CAL_DEFAULT;
  t12_io_sim_init(&s_sim_dev, &s_io, &plant_params, TICK_DT_S, DEFAULT_AMBIENT_C);
#ifdef KRUMPIR_IO_HIL
  t12_io_hil_init(&s_hil_dev, &s_io, TICK_DT_S); /* replaces the simulated tip behind s_io */
#endif
  t12_loop_init(&s_loop, &s_ctrl_default, &safety_cfg, &cal, TICK_DT_S);
}

static void control_tick(void *arg, uint32_t tick)
{
#ifdef KRUMPIR_XCP
  DaqCreateEvent(tick);  /* every 1 ms tick */
  DaqCreateEvent(cycle); /* every completed heater cycle (about 10 Hz) */
  ctrl_gen_cal_apply(&s_rte);
#endif

  osal_crit_enter(&s_sim_mux);
  sim_cfg_t sim_cfg = s_sim;
  osal_crit_exit(&s_sim_mux);
  if (sim_cfg.coeffs_gen != s_applied_coeffs_gen) {
    t12_plant_set_coeffs(&s_sim_dev.plant, &sim_cfg.coeffs);
    s_applied_coeffs_gen = sim_cfg.coeffs_gen;
  }
  s_sim_dev.plant.fault = sim_cfg.fault;
  s_sim_dev.ambient = sim_cfg.ambient;

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
  bool fresh = t12_loop_tick(&s_loop, &s_io);
#ifdef KRUMPIR_IO_HIL
  t12_io_hil_step(&s_hil_dev);
#else
  t12_io_sim_step(&s_sim_dev);
#endif
  s_trip = s_loop.safety.latched;
  rte_tick_end(&s_rte); /* a trip or a refused start drops the run request */
#ifdef KRUMPIR_IO_HIL
  hil_port_tick_end(); /* heater and series_on out */
#endif

  if (fresh) {
    s_temp = s_loop.temp;
    s_handle = s_loop.handle;
    s_amps = s_loop.amps;
    s_duty = s_loop.duty;
    telem_sample_t s = {tick, CTRL_GEN_TELEM_VALUES};
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
  cfg.stack = CONTROL_STACK_BYTES;
  tick_task_start(&cfg, control_init, control_tick, NULL);
}

rte_t *control_task_rte(void) { return &s_rte; }

void control_task_set_load(float w_per_k)
{
  /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
  t12_params_t params = T12_PARAMS_DEFAULT;
  params.g_load = w_per_k;
  t12_coeffs_t coeffs;
  t12_plant_coeffs(&params, TICK_DT_S, &coeffs);

  osal_crit_enter(&s_sim_mux);
  s_sim.load = w_per_k;
  s_sim.coeffs = coeffs;
  s_sim.coeffs_gen++;
  osal_crit_exit(&s_sim_mux);
}

void control_task_set_fault(t12_fault_t fault)
{
  osal_crit_enter(&s_sim_mux);
  s_sim.fault = fault;
  osal_crit_exit(&s_sim_mux);
}

void control_task_set_ambient(float celsius)
{
  osal_crit_enter(&s_sim_mux);
  s_sim.ambient = celsius;
  osal_crit_exit(&s_sim_mux);
}

void control_task_get_sim(float *load, t12_fault_t *fault, float *ambient)
{
  osal_crit_enter(&s_sim_mux);
  *load = s_sim.load;
  *fault = s_sim.fault;
  *ambient = (float)s_sim.ambient;
  osal_crit_exit(&s_sim_mux);
}

void control_task_get_state(control_state_t *s)
{
  s->temp = s_temp;
  s->handle = s_handle;
  s->amps = s_amps;
  s->duty = s_duty;
}
