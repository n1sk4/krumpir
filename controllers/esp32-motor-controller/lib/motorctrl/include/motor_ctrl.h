#ifndef MOTOR_CTRL_H
#define MOTOR_CTRL_H

#include <stdbool.h>

#include "pid.h"

/* Speed controller: PID on the measured speed, output = duty -1..+1, updated every control tick.
 * The setpoint is clamped to sp_min..sp_max. */
typedef struct {
  float kp, ki, kd;     /* duty per rpm, per rpm*s, per rpm/s */
  float sp_min, sp_max; /* [rpm] */
} motor_ctrl_cfg_t;

/* Tuned gains (method and results in docs/tuning.md). */
#define MOTOR_CTRL_CFG_DEFAULT {7.2e-4f, 0.03f, 0.0f, -10000.0f, 10000.0f}

typedef struct {
  pid_ctrl_t pid;
  motor_ctrl_cfg_t cfg;
  float setpoint; /* [rpm] */
  bool run;
} motor_ctrl_t;

void motor_ctrl_init(motor_ctrl_t *c, const motor_ctrl_cfg_t *cfg, float dt);
void motor_ctrl_set_setpoint(motor_ctrl_t *c, float sp); /* clamped to sp_min..sp_max */
void motor_ctrl_set_gains(motor_ctrl_t *c, float kp, float ki, float kd);
/* Start and stop both restart the PID from zero. */
void motor_ctrl_start(motor_ctrl_t *c);
void motor_ctrl_stop(motor_ctrl_t *c);
/* Returns the duty for this tick: 0 while stopped. */
float motor_ctrl_update(motor_ctrl_t *c, float speed_rpm);

#endif
