#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "gen/ctrl_gen.h" /* parameter ids, generated from controller.toml */
#include "t12_plant.h"

/* Starts the 1 kHz control task on core 1. */
void control_task_start(void);

/* The controller's runtime: parameters, run/stop/clear and the latched trip, safe from any task. */
rte_t *control_task_rte(void);

/* Simulation only: extra heat loss of touching a joint [W/K], an injected fault and the ambient. */
void control_task_set_load(float w_per_k);
void control_task_set_fault(t12_fault_t fault);
void control_task_set_ambient(float celsius);
void control_task_get_sim(float *load, t12_fault_t *fault, float *ambient);

typedef struct {
  float temp, handle, amps, duty;
} control_state_t;
void control_task_get_state(control_state_t *s);

#endif
