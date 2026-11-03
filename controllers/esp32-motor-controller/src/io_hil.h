#ifndef IO_HIL_H
#define IO_HIL_H

#include "hil_port.h"
#include "motor_io.h"

/* motor_io_t backend on the HIL link (controller.toml [hil]): the speed is plant value 0 (the
 * FMU's speed_rpm), the duty goes out as command 0. A reading older than HIL_MAX_AGE ticks counts
 * as lost (NaN), so a dead link trips the controller's sensor check. */
typedef struct {
  hil_slots_t slots;
} motor_io_hil_t;

#define HIL_MAX_AGE 10 /* ticks a speed is held before it counts as lost */

void motor_io_hil_init(motor_io_hil_t *h, motor_io_t *io_out);

#endif
