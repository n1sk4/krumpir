#include "console.h"

#include <stdio.h>
#include <string.h>

#include "control_task.h"
#include "esp_console.h"
#include "param_parse.h"
#include "telemetry.h"

#define SIM_LOAD_MAX_NM 0.1f
#define SIM_INERTIA_MIN 0.1f
#define SIM_INERTIA_MAX 10.0f
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
  if (!arg_float(argc, argv, -CTRL_SP_MAX_RPM, CTRL_SP_MAX_RPM, &v))
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
  if (!arg_float(argc, argv, 0.0f, CTRL_GAIN_MAX, &v))
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
  control_task_clear_fault();
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
  fault_t fault = control_task_get_fault();
  if (fault != FAULT_NONE) {
    printf("error: fault (%s) is latched, use clear first\n", fault_name(fault));
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
  float rpm, duty;
  control_task_get_state(&rpm, &duty);
  loop_stats_t period, exec;
  uint32_t overruns;
  control_task_get_stats(&period, &exec, &overruns);

  float load, inertia;
  control_task_get_sim(&load, &inertia);
  printf("%s  sp %g rpm  kp %g ki %g kd %g\n", p.run ? "running" : "stopped", p.sp, p.kp, p.ki,
         p.kd);
  printf("fault %s  sim load %g N*m  inertia x%g\n", fault_name(control_task_get_fault()), load,
         inertia);
  printf("speed %.1f rpm  duty %.3f  log %s  telemetry dropped %u\n", rpm, duty,
         telemetry_log() ? "on" : "off", (unsigned)telemetry_dropped());
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
  repl_cfg.prompt = "motor> ";
  repl_cfg.task_core_id = 0; /* keep the console off the control core */
  esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

  esp_console_register_help_command();
  reg("sp", "Set speed setpoint [rpm]", "<rpm>", cmd_sp);
  reg("kp", "Set proportional gain", "<value>", cmd_gain);
  reg("ki", "Set integral gain", "<value>", cmd_gain);
  reg("kd", "Set derivative gain", "<value>", cmd_gain);
  reg("load", "Simulation only: load torque on the shaft [N*m]", "<Nm>", cmd_load);
  reg("inertia", "Simulation only: rotor inertia scale (1 = nominal)", "<scale>", cmd_inertia);
  reg("clear", "Clear a latched fault", NULL, cmd_clear);
  reg("start", "Start the controller", NULL, cmd_start);
  reg("stop", "Stop: duty 0, PID reset", NULL, cmd_stop);
  reg("status", "Show state and loop timing", NULL, cmd_status);
  reg("log", "CSV telemetry on or off", "on|off", cmd_log);

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
