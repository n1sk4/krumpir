#ifndef MOTOR_LOOP_H
#define MOTOR_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "motor_ctrl.h"
#include "motor_io.h"
#include "motor_safety.h"

/* The whole controller, advanced once per control tick: read the speed, supervise, control. A trip
 * stops the controller and puts the driver in its safe state. The firmware task and the tests both
 * run this. */
typedef struct {
  motor_ctrl_t ctrl;
  motor_safety_t safety;
  float speed; /* last reading [rpm] */
  float duty;  /* duty of the last tick */
} motor_loop_t;

void motor_loop_init(motor_loop_t *l, const motor_ctrl_cfg_t *ctrl,
                     const motor_safety_cfg_t *safety, float tick_dt_s);
void motor_loop_tick(motor_loop_t *l, const motor_io_t *io);
bool motor_loop_start(motor_loop_t *l); /* refused (false) while a trip is latched */
void motor_loop_stop(motor_loop_t *l);
void motor_loop_clear(motor_loop_t *l); /* clears a trip */

#endif
