#ifndef CONSOLE_UTIL_H
#define CONSOLE_UTIL_H

#include <stdbool.h>

#include "esp_console.h"

/* A serial console on UART0 (the USB-serial bridge), built on esp_console, plus the pieces every
 * controller needs. Usage:
 *   console_init("motor> ");           creates the REPL, registers `help` and `log on|off`
 *   console_register("sp", ...);       the controller's own commands
 *   console_run();                     starts the REPL task on core 0 */

void console_init(const char *prompt);
void console_register(const char *name, const char *help, const char *hint,
                      esp_console_cmd_func_t fn);
void console_run(void);

/* Parses argv[1] into *out within [lo, hi]. Prints the usage or the reason and returns false on
 * failure, so a command can simply `return 1`. */
bool console_arg_float(int argc, char **argv, float lo, float hi, float *out);

/* Prints the "loop (since last status)" line (period and execution time in ns, overruns) when the
 * control loop has run since the last call, and restarts that window. Call it from `status`. */
void console_print_loop_stats(void);

#endif
