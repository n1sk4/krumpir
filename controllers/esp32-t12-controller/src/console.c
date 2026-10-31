#include "console.h"

#include <stdio.h>
#include <string.h>

#include "control_task.h"
#include "esp_console.h"
#include "param_parse.h"
#include "telemetry.h"

#define GAIN_MAX 1000.0f
#define RAMP_MAX 100.0f
#define LOAD_MAX_W_PER_K 1.0f
#define AMBIENT_MIN_C (-20.0f)
#define AMBIENT_MAX_C 60.0f
#define NS(cycles) ((uint64_t)(cycles) * 1000 / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

/* Parses argv[1] into *out within [lo, hi]; prints the reason and returns false on failure. */
static bool arg_float(int argc, char **argv, float lo, float hi, float *out)
{
  if (argc != 2) {
    printf("usage: %s <value>\n", argv[0]);
    return false;
  }
  if (!parse_float(argv[1], lo, hi, out)) {
    printf("error: %s is not a number in [%g, %g]\n", argv[1], lo, hi);
    return false;
  }
  return true;
}

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

static int cmd_log(int argc, char **argv)
{
  if (argc != 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
    printf("usage: log on|off\n");
    return 1;
  }
  telemetry_set_log(!strcmp(argv[1], "on"));
  return 0;
}

static int cmd_status(int argc, char **argv)
{
  ctrl_params_t p;
  control_task_get_params(&p);
  control_state_t st;
  control_task_get_state(&st);
  loop_stats_t period, exec;
  uint32_t overruns;
  control_task_get_stats(&period, &exec, &overruns);

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
  if (period.count) {
    printf("loop (since last status): period ns min/mean/max %llu/%llu/%llu  "
           "exec ns min/mean/max %llu/%llu/%llu  overruns %u  n=%u\n",
           NS(period.min), NS(loop_stats_mean(&period)), NS(period.max), NS(exec.min),
           NS(loop_stats_mean(&exec)), NS(exec.max), (unsigned)overruns, (unsigned)period.count);
  }
  return 0;
}

static void reg(const char *name, const char *help, const char *hint, esp_console_cmd_func_t fn)
{
  esp_console_cmd_t cmd = {.command = name, .help = help, .hint = hint, .func = fn};
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void console_start(void)
{
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_cfg.prompt = "t12> ";
  repl_cfg.task_core_id = 0; /* keep the console off the control core */
  esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

  esp_console_register_help_command();
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
  reg("log", "CSV telemetry on or off", "on|off", cmd_log);

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
