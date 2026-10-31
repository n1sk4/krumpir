#ifndef T12_CTRL_H
#define T12_CTRL_H

#include <stdbool.h>

#include "pid.h"

/* Temperature controller: PID on the measured tip temperature, output = heating duty 0..1,
 * updated once per measurement (once per heater cycle). The setpoint is clamped to the allowed
 * range and, with a ramp rate set, approached no faster than that rate. */
typedef struct {
  float kp, ki, kd; /* duty per K, per K*s, per K/s */
  float sp_min, sp_max; /* [degC] */
  float ramp;           /* [K/s], 0 = jump straight to the setpoint */
} t12_ctrl_cfg_t;

/* Placeholder gains tuned on the simulated tip; re-tune on the real one. */
#define T12_CTRL_CFG_DEFAULT {0.05f, 0.05f, 0.0f, 100.0f, 450.0f, 0.0f}

typedef struct {
  pid_ctrl_t pid;
  t12_ctrl_cfg_t cfg;
  float setpoint;  /* requested [degC] */
  float sp_active; /* setpoint after the ramp */
  bool run;
} t12_ctrl_t;

void t12_ctrl_init(t12_ctrl_t *c, const t12_ctrl_cfg_t *cfg, float dt);
void t12_ctrl_set_setpoint(t12_ctrl_t *c, float sp); /* clamped to sp_min..sp_max */
void t12_ctrl_set_gains(t12_ctrl_t *c, float kp, float ki, float kd);

/* The ramp starts from the current temperature; the PID restarts from zero. */
void t12_ctrl_start(t12_ctrl_t *c, float temp);
void t12_ctrl_stop(t12_ctrl_t *c);

/* Returns the duty for the next cycle: 0 while stopped, the last duty on a non-finite reading. */
float t12_ctrl_update(t12_ctrl_t *c, float temp);

#endif
