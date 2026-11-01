#ifndef CONSOLE_UTIL_H
#define CONSOLE_UTIL_H

#include <stdbool.h>

/* A serial command console plus the pieces every controller needs (the ESP-IDF port runs it on
 * UART0, the USB-serial bridge). Usage:
 *   console_init("motor> ");           creates the console, registers `help` and `log on|off`
 *   console_register("sp", ...);       the controller's own commands
 *   console_run();                     starts the console task */

/* A command: argv[0] is its name. Returns 0 on success. */
typedef int (*console_cmd_fn_t)(int argc, char **argv);

void console_init(const char *prompt);
void console_register(const char *name, const char *help, const char *hint, console_cmd_fn_t fn);
void console_run(void);

/* Parses argv[1] into *out within [lo, hi]. Prints the usage or the reason and returns false on
 * failure, so a command can simply `return 1`. */
bool console_arg_float(int argc, char **argv, float lo, float hi, float *out);

/* Prints the "loop (since last status)" line (period and execution time in ns, overruns) when the
 * control loop has run since the last call, and restarts that window. Call it from `status`. */
void console_print_loop_stats(void);

#endif
