#include "console.h"

#include <stdio.h>
#include <string.h>

#include "console_util.h"
#include "control_task.h"
#include "gen/ctrl_gen.h"
#include "telemetry.h"

#define LOAD_MAX_W_PER_K 1.0f
#define AMBIENT_MIN_C (-20.0f)
#define AMBIENT_MAX_C 60.0f
#define arg_float console_arg_float

int cmd_load(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, 0.0f, LOAD_MAX_W_PER_K, &v))
    return 1;
  control_task_set_load(v);
  return 0;
}

int cmd_ambient(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, AMBIENT_MIN_C, AMBIENT_MAX_C, &v))
    return 1;
  control_task_set_ambient(v);
  return 0;
}

static const char *fault_label(t12_fault_t f)
{
  switch (f) {
  case T12_FAULT_NONE: return "none";
  case T12_FAULT_OPEN: return "open";
  case T12_FAULT_SHORT: return "short";
  case T12_FAULT_STUCK_ON: return "stuck";
  }
  return "?";
}

int cmd_fault(int argc, char **argv)
{
  for (t12_fault_t f = T12_FAULT_NONE; f <= T12_FAULT_STUCK_ON; f++) {
    if (argc == 2 && !strcmp(argv[1], fault_label(f))) {
      control_task_set_fault(f);
      return 0;
    }
  }
  printf("usage: fault none|open|short|stuck\n");
  return 1;
}

int cmd_status(int argc, char **argv)
{
  rte_t *r = control_task_rte();
  control_state_t st;
  control_task_get_state(&st);
  const char *trip = rte_trip(r);

  float load, ambient;
  t12_fault_t fault;
  control_task_get_sim(&load, &fault, &ambient);
  printf("%s  sp %g C  kp %g ki %g kd %g  ramp %g K/s\n", rte_running(r) ? "running" : "stopped",
         rte_get(r, PARAM_SP), rte_get(r, PARAM_KP), rte_get(r, PARAM_KI), rte_get(r, PARAM_KD),
         rte_get(r, PARAM_RAMP));
  printf("trip %s  sim load %g W/K  fault %s  ambient %g C\n", trip ? trip : "none", load,
         fault_label(fault), ambient);
  printf("tip %.1f C  handle %.1f C  duty %.3f  heater %.2f A  log %s  telemetry dropped %u\n",
         st.temp, st.handle, st.duty, st.amps, telemetry_log() ? "on" : "off",
         (unsigned)telemetry_dropped());
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
