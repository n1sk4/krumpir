#include "console.h"

#include <stdio.h>
#include <string.h>

#include "console_util.h"
#include "control_task.h"
#include "telemetry.h"

#define SIM_LOAD_MAX_NM 0.1f
#define SIM_INERTIA_MIN 0.1f
#define SIM_INERTIA_MAX 10.0f
#define arg_float console_arg_float

static int cmd_sp(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, -SP_MAX_RPM, SP_MAX_RPM, &v))
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

static int cmd_load(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, -SIM_LOAD_MAX_NM, SIM_LOAD_MAX_NM, &v))
    return 1;
  control_task_set_load(v);
  return 0;
}

static int cmd_inertia(int argc, char **argv)
{
  float v;
  if (!arg_float(argc, argv, SIM_INERTIA_MIN, SIM_INERTIA_MAX, &v))
    return 1;
  control_task_set_inertia_scale(v);
  return 0;
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
  motor_trip_t trip = control_task_get_trip();
  if (trip != MOTOR_TRIP_NONE) {
    printf("error: trip (%s) is latched, use clear first\n", motor_trip_name(trip));
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

  float load, inertia;
  control_task_get_sim(&load, &inertia);
  printf("%s  sp %g rpm  kp %g ki %g kd %g\n", p.run ? "running" : "stopped", p.sp, p.kp, p.ki,
         p.kd);
  printf("trip %s  sim load %g N*m  inertia x%g\n", motor_trip_name(control_task_get_trip()),
         load, inertia);
  printf("speed %.1f rpm  duty %.3f  log %s  telemetry dropped %u\n", st.speed, st.duty,
         telemetry_log() ? "on" : "off", (unsigned)telemetry_dropped());
  console_print_loop_stats();
  return 0;
}

#define reg console_register

void console_start(void)
{
  console_init("motor> ");
  reg("sp", "Set speed setpoint [rpm]", "<rpm>", cmd_sp);
  reg("kp", "Set proportional gain", "<value>", cmd_gain);
  reg("ki", "Set integral gain", "<value>", cmd_gain);
  reg("kd", "Set derivative gain", "<value>", cmd_gain);
  reg("load", "Simulation only: load torque on the shaft [N*m]", "<Nm>", cmd_load);
  reg("inertia", "Simulation only: rotor inertia scale (1 = nominal)", "<scale>", cmd_inertia);
  reg("clear", "Clear a latched trip", NULL, cmd_clear);
  reg("start", "Start the controller", NULL, cmd_start);
  reg("stop", "Stop: duty 0, PID reset", NULL, cmd_stop);
  reg("status", "Show state and loop timing", NULL, cmd_status);

  console_run();
}
