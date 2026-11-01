#ifndef CONSOLE_HOST_H
#define CONSOLE_HOST_H

/* Host port of the OSAL console (console_util.h): commands are kept in a table and run from a
 * string, so tests can drive a controller's console without a serial port. */

/* Runs one command line ("sp 300"). Returns the command's result, or -1 for an unknown command. */
int console_host_exec(const char *line);

/* The help text a command was registered with, or NULL. */
const char *console_host_help(const char *name);

#endif
