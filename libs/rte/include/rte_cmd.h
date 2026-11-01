#ifndef RTE_CMD_H
#define RTE_CMD_H

#include "rte.h"

/* Console commands for a runtime, on the OSAL console (console_util.h). One runtime per firmware.
 *
 * rte_cmd_register_params: one command per parameter, `<name> <value>`, range-checked, with the
 *   parameter's help and hint.
 * rte_cmd_register_run: clear, start (refused while a trip is latched) and stop.
 * Two calls, so the controller can put its own commands in between (help lists them in order). */
void rte_cmd_register_params(rte_t *r);
void rte_cmd_register_run(rte_t *r);

#endif
