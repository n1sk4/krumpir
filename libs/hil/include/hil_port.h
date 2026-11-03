#ifndef HIL_PORT_H
#define HIL_PORT_H

/* The controller's end of the HIL link: what a platform port provides (libs/osal_host on a PC, a
 * board's UART later). The control task binds its slots once, before tick_task_start(); the port
 * copies the newest plant values in before each tick and sends the commands after it. The port's
 * tick task starts with the first PLANT frame, so the controller never runs without the plant. */

#include <stddef.h>
#include <stdint.h>

#include "hil.h"

typedef struct {
  float plant[HIL_MAX_VALUES]; /* the newest plant values, in controller.toml [hil] order */
  float cmd[HIL_MAX_VALUES];   /* the tick's commands, likewise */
  uint32_t age;                /* ticks since the plant values came: 0 = fresh for this tick */
} hil_slots_t;

/* The names give the HELLO hash the runner checks (hil_names_hash). */
void hil_port_bind(hil_slots_t *s, const char *const *plant, size_t n_plant,
                   const char *const *cmd, size_t n_cmd);

#endif
