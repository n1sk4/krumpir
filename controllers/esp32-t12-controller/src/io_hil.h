#ifndef IO_HIL_H
#define IO_HIL_H

#include <stdbool.h>
#include <stdint.h>

#include "hil_port.h"
#include "t12_io.h"

/* t12_io_t backend on the HIL link (controller.toml [hil]). The plant values are the FMU's
 * physical ones (v_amp, i_heater, t_handle_c), turned into ADC counts by the same front end as
 * the on-chip simulation (t12_frontend.h). Commands: heater (Q1) and series_on, the series switch
 * Q3, which stays on while the heartbeat toggles, as on the board. A reading older than
 * HIL_MAX_AGE ticks reads as a saturated thermocouple (invalid), so a dead link trips the
 * controller's sensor check. */
typedef struct {
  hil_slots_t slots;
  uint32_t hb_age, hb_timeout; /* ticks since the last heartbeat toggle, and the Q3 drop-out */
} t12_io_hil_t;

void t12_io_hil_init(t12_io_hil_t *h, t12_io_t *io_out, double dt);
void t12_io_hil_step(t12_io_hil_t *h); /* once per tick, after the controller: Q3 */

#endif
