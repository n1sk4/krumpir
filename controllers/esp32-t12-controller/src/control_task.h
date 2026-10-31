#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "loop_stats.h"
#include "t12_plant.h"
#include "t12_safety.h"

#define SP_MIN_C 100.0f
#define SP_MAX_C 450.0f

/* Runtime-adjustable settings, handed from the console to the control task. */
typedef struct {
  float sp; /* setpoint [degC] */
  float kp, ki, kd;
  float ramp; /* [K/s], 0 = off */
  bool run;
} ctrl_params_t;

/* Starts the 1 kHz control task on core 1. */
void control_task_start(void);

/* Settings are copied by the control task once per tick, so a change never tears. */
void control_task_set_params(const ctrl_params_t *p);
void control_task_get_params(ctrl_params_t *p);

/* Simulation only: extra heat loss of touching a joint [W/K], an injected fault and the ambient. */
void control_task_set_load(float w_per_k);
void control_task_set_fault(t12_fault_t fault);
void control_task_set_ambient(float celsius);
void control_task_get_sim(float *load, t12_fault_t *fault, float *ambient);

/* A trip stops the controller and stays latched until cleared. */
t12_trip_t control_task_get_trip(void);
void control_task_clear_trip(void);

typedef struct {
  float temp, handle, amps, duty;
} control_state_t;
void control_task_get_state(control_state_t *s);

/* Snapshot of period / exec-time stats (in CPU cycles) and missed ticks, then resets them. */
void control_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns);

#endif
