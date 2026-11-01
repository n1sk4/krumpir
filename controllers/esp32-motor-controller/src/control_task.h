#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "gen/ctrl_gen.h" /* parameter ids, generated from controller.toml */

/* Starts the 1 kHz control task on core 1. Boots stopped, with the tuned gains. */
void control_task_start(void);

/* The controller's runtime: parameters, run/stop/clear and the latched trip, safe from any task. */
rte_t *control_task_rte(void);

/* Simulation only: disturbance torque on the motor shaft and rotor inertia scale (1 = nominal). */
void control_task_set_load(float torque_nm);
void control_task_set_inertia_scale(float scale);
void control_task_get_sim(float *load_nm, float *inertia_scale);

typedef struct {
  float speed, duty; /* [rpm], -1..+1 */
} control_state_t;
void control_task_get_state(control_state_t *s);

#endif
