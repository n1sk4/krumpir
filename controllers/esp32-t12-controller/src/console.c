#include "console.h"

#include <stdio.h>
#include <string.h>

#include "console_util.h"
#include "control_task.h"
#include "telemetry.h"

#define GAIN_MAX 1000.0f
#define RAMP_MAX 100.0f
#define LOAD_MAX_W_PER_K 1.0f
#define AMBIENT_MIN_C (-20.0f)
#define AMBIENT_MAX_C 60.0f
#define arg_float console_arg_float

static int cmd_sp(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, SP_MIN_C, SP_MAX_C, &v))
    return 1;
  ctrl_params_t p;
  control_task_get_params(&p);
  p.sp = v;
  control_task_set_params(&p);
  return 0;
}

static int cmd_gain(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, 0.0f, GAIN_MAX, &v))
    return 1;
  ctrl_params_t p;
  control_task_get_params(&p);
  float *gain = !strcmp(argv[0], "kp") ? &p.kp : !strcmp(argv[0], "ki") ? &p.ki : &p.kd;
  *gain = v;
  control_task_set_params(&p);
  return 0;
}

static int cmd_ramp(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, 0.0f, RAMP_MAX, &v))
    return 1;
  ctrl_params_t p;
  control_task_get_params(&p);
  p.ramp = v;
  control_task_set_params(&p);
  return 0;
}

static int cmd_load(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, 0.0f, LOAD_MAX_W_PER_K, &v))
    return 1;
  control_task_set_load(v);
  return 0;
}

static int cmd_ambient(int argc, char **argv)
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

static int cmd_fault(int argc, char **argv)
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

static int cmd_clear(int argc, char **argv)
{
  control_task_clear_trip();
  return 0;
}

static int set_run(bool run)
{
  ctrl_params_t p;
  control_task_get_params(&p);
  p.run = run;
  control_task_set_params(&p);
  return 0;
}

static int cmd_start(int argc, char **argv)
{
  t12_trip_t trip = control_task_get_trip();
  if (trip != T12_TRIP_NONE) {
    printf("error: trip (%s) is latched, use clear first\n", t12_trip_name(trip));
    return 1;
  }
  return set_run(true);
}
static int cmd_stop(int argc, char **argv) { return set_run(false); }

static int cmd_status(int argc, char **argv)
{
  ctrl_params_t p;
  control_task_get_params(&p);
  control_state_t st;
  control_task_get_state(&st);

  float load, ambient;
  t12_fault_t fault;
  control_task_get_sim(&load, &fault, &ambient);
  printf("%s  sp %g C  kp %g ki %g kd %g  ramp %g K/s\n", p.run ? "running" : "stopped", p.sp, p.kp,
         p.ki, p.kd, p.ramp);
  printf("trip %s  sim load %g W/K  fault %s  ambient %g C\n",
         t12_trip_name(control_task_get_trip()), load, fault_label(fault), ambient);
  printf("tip %.1f C  handle %.1f C  duty %.3f  heater %.2f A  log %s  telemetry dropped %u\n",
         st.temp, st.handle, st.duty, st.amps, telemetry_log() ? "on" : "off",
         (unsigned)telemetry_dropped());
  console_print_loop_stats();
  return 0;
}

#define reg console_register

void console_start(void)
{
  console_init("t12> ");
  reg("sp", "Set temperature setpoint [degC]", "<degC>", cmd_sp);
  reg("kp", "Set proportional gain", "<value>", cmd_gain);
  reg("ki", "Set integral gain", "<value>", cmd_gain);
  reg("kd", "Set derivative gain", "<value>", cmd_gain);
  reg("ramp", "Setpoint ramp rate [K/s], 0 = off", "<K/s>", cmd_ramp);
  reg("load", "Simulation only: extra heat loss of touching a joint [W/K]", "<W/K>", cmd_load);
  reg("ambient", "Simulation only: ambient temperature [degC]", "<degC>", cmd_ambient);
  reg("fault", "Simulation only: inject a tip fault", "none|open|short|stuck", cmd_fault);
  reg("clear", "Clear a latched trip", NULL, cmd_clear);
  reg("start", "Start the controller", NULL, cmd_start);
  reg("stop", "Stop: duty 0, PID reset", NULL, cmd_stop);
  reg("status", "Show state and loop timing", NULL, cmd_status);

  console_run();
}
