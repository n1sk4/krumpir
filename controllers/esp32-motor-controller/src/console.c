#include "console.h"

#include <stdio.h>

#include "console_util.h"
#include "control_task.h"
#include "gen/ctrl_gen.h"
#include "telemetry.h"

#define SIM_LOAD_MAX_NM 0.1f
#define SIM_INERTIA_MIN 0.1f
#define SIM_INERTIA_MAX 10.0f

int cmd_load(int argc, char **argv)
{
  float v;
  if (!console_arg_float(argc, argv, -SIM_LOAD_MAX_NM, SIM_LOAD_MAX_NM, &v))
    return 1;
  control_task_set_load(v);
  return 0;
}

int cmd_inertia(int argc, char **argv)
{
  float v;
  if (!console_arg_float(argc, argv, SIM_INERTIA_MIN, SIM_INERTIA_MAX, &v))
    return 1;
  control_task_set_inertia_scale(v);
  return 0;
}

int cmd_status(int argc, char **argv)
{
  rte_t *r = control_task_rte();
  control_state_t st;
  control_task_get_state(&st);
  const char *trip = rte_trip(r);

  float load, inertia;
  control_task_get_sim(&load, &inertia);
  printf("%s  sp %g rpm  kp %g ki %g kd %g\n", rte_running(r) ? "running" : "stopped",
         rte_get(r, PARAM_SP), rte_get(r, PARAM_KP), rte_get(r, PARAM_KI), rte_get(r, PARAM_KD));
  printf("trip %s  sim load %g N*m  inertia x%g\n", trip ? trip : "none", load, inertia);
  printf("speed %.1f rpm  duty %.3f  log %s  telemetry dropped %u\n", st.speed, st.duty,
         telemetry_log() ? "on" : "off", (unsigned)telemetry_dropped());
  console_print_loop_stats();
  return 0;
}

/* The command handlers above are registered by the generated ctrl_gen_console_register(), in
 * the order of controller.toml. */
void console_start(void)
{
  ctrl_gen_console_register(control_task_rte());
  console_run();
}
