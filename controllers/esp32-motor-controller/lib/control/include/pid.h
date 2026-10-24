#ifndef PID_H
#define PID_H

#include <stdbool.h>

/* Parallel PID, derivative on filtered measurement, clamping anti-windup.
 * Units are up to the caller; dt is fixed at init. */
typedef struct {
    float kp, ki, kd;
    float out_min, out_max;
    float dt;       /* [s] */
    float d_tau;    /* derivative low-pass time constant [s]; 0 = no filter */
    float integ;    /* integrator state (already multiplied by ki) */
    float meas_prev;
    float d_filt;
    float last_out; /* returned on non-finite input */
    bool primed;    /* false until first update, avoids a derivative spike */
} pid_ctrl_t;

void pid_init(pid_ctrl_t *p, float kp, float ki, float kd, float out_min, float out_max,
              float dt, float d_tau);
void pid_set_gains(pid_ctrl_t *p, float kp, float ki, float kd);
void pid_reset(pid_ctrl_t *p);
float pid_update(pid_ctrl_t *p, float setpoint, float measurement);

#endif
