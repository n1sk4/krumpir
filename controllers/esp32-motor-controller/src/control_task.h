#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "motor_ctrl.h"
#include "motor_safety.h"

#define SP_MAX_RPM 10000.0f
#define GAIN_MAX 1000000.0f

/* Runtime-adjustable settings, handed from the console to the control task. */
typedef struct {
  float sp; /* setpoint [rpm] */
  float kp, ki, kd;
  bool run;
} ctrl_params_t;

/* Starts the 1 kHz control task on core 1. Boots stopped, with the tuned gains. */
void control_task_start(void);

/* Settings are copied by the control task once per tick, so a change never tears. */
void control_task_set_params(const ctrl_params_t *p);
void control_task_get_params(ctrl_params_t *p);

/* Simulation only: disturbance torque on the motor shaft and rotor inertia scale (1 = nominal). */
void control_task_set_load(float torque_nm);
void control_task_set_inertia_scale(float scale);
void control_task_get_sim(float *load_nm, float *inertia_scale);

/* A trip stops the controller and stays latched until cleared. */
motor_trip_t control_task_get_trip(void);
void control_task_clear_trip(void);

typedef struct {
  float speed, duty; /* [rpm], -1..+1 */
} control_state_t;
void control_task_get_state(control_state_t *s);

#endif
