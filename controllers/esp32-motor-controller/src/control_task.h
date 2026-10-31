#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>

#include "ctrl_params.h"
#include "safety.h"

/* Starts the 1 kHz control task on core 1. */
void control_task_start(void);

/* Settings are copied by the control task once per cycle, so a change never tears. */
void control_task_set_params(const ctrl_params_t *p);
void control_task_get_params(ctrl_params_t *p);

/* Simulation only: disturbance torque on the motor shaft and rotor inertia scale (1 = nominal). */
void control_task_set_load(float torque_nm);
void control_task_set_inertia_scale(float scale);
void control_task_get_sim(float *load_nm, float *inertia_scale);

/* A fault stops the controller and stays latched until cleared. */
fault_t control_task_get_fault(void);
void control_task_clear_fault(void);

/* Latest simulated speed [rpm] and duty (-1..+1). */
void control_task_get_state(float *rpm, float *duty);

#endif
