#include "console_util.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "param_parse.h"
#include "sdkconfig.h"
#include "telemetry.h"
#include "tick_task.h"

#define NS(cycles) ((uint64_t)(cycles) * 1000 / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static esp_console_repl_t *s_repl;

bool console_arg_float(int argc, char **argv, float lo, float hi, float *out)
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

static int cmd_log(int argc, char **argv)
{
  if (argc != 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
    printf("usage: log on|off\n");
    return 1;
  }
  telemetry_set_log(!strcmp(argv[1], "on"));
  return 0;
}

void console_print_loop_stats(void)
{
  loop_stats_t period, exec;
  uint32_t overruns;
  tick_task_get_stats(&period, &exec, &overruns);
  if (!period.count)
    return;
  printf("loop (since last status): period ns min/mean/max %llu/%llu/%llu  "
         "exec ns min/mean/max %llu/%llu/%llu  overruns %u  n=%u\n",
         NS(period.min), NS(loop_stats_mean(&period)), NS(period.max), NS(exec.min),
         NS(loop_stats_mean(&exec)), NS(exec.max), (unsigned)overruns, (unsigned)period.count);
}

void console_register(const char *name, const char *help, const char *hint, console_cmd_fn_t fn)
{
  esp_console_cmd_t cmd = {.command = name, .help = help, .hint = hint, .func = fn};
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void console_init(const char *prompt)
{
  esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_cfg.prompt = prompt;
  repl_cfg.task_core_id = 0; /* keep the console off the control core */
  esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &s_repl));

  esp_console_register_help_command();
  console_register("log", "CSV telemetry on or off", "on|off", cmd_log);
}

void console_run(void) { ESP_ERROR_CHECK(esp_console_start_repl(s_repl)); }
