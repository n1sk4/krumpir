#ifndef HIL_PORT_H
#define HIL_PORT_H

/* The controller's end of the HIL link, as the control task uses it (src/control_task.c under
 * KRUMPIR_IO_HIL). hil_port.c implements it on one hil_link_t; a platform supplies the line:
 * hil_port_write() and a reader that calls hil_port_feed() (libs/osal_host/src/hil_host.c on a
 * PC, libs/esp/espglue/src/hil_uart.c on the ESP32).
 *
 *   hil_port_bind(...)        once, before tick_task_start()
 *   every tick: hil_port_tick_begin(); [hil_port_bye() -> stop]; the controller; hil_port_tick_end();
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hil.h"

typedef struct {
  float plant[HIL_MAX_VALUES]; /* the newest plant values, in controller.toml [hil] order */
  float cmd[HIL_MAX_VALUES];   /* the tick's commands, likewise */
  uint32_t age;                /* ticks since the plant values came: 0 = fresh for this tick */
} hil_slots_t;

/* slots.age before the first plant frame: the controller waits for the runner (it does not run
 * on no plant at all, which would trip its sensor checks before the run starts) */
#define HIL_AGE_NONE (UINT32_MAX / 2)

/* The names give the HELLO hash the runner checks (hil_names_hash). */
void hil_port_bind(hil_slots_t *s, const char *const *plant, size_t n_plant,
                   const char *const *cmd, size_t n_cmd);

void hil_port_tick_begin(void); /* the newest plant values into the slots */
void hil_port_tick_end(void);   /* the slots' commands out */
bool hil_port_bye(void);        /* true once after the runner said BYE (unload or fault) */

/* The platform's side */
void hil_port_write(const uint8_t *buf, size_t n); /* implemented by the platform; quick */
/* Called by its reader with each received byte. Returns the type of a frame it completed
 * (HIL_HELLO, HIL_PLANT, HIL_BYE), -HIL_HELLO for a HELLO for other values, else 0. */
int hil_port_feed(uint8_t b);

#endif
