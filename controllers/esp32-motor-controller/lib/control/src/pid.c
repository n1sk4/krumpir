#include "pid.h"

#include <math.h>

void pid_reset(pid_ctrl_t *p)
{
  p->integ = 0.0f;
  p->meas_prev = 0.0f;
  p->d_filt = 0.0f;
  p->last_out = 0.0f;
  p->primed = false;
}

void pid_init(pid_ctrl_t *p, float kp, float ki, float kd, float out_min, float out_max, float dt,
              float d_tau)
{
  p->out_min = out_min;
  p->out_max = out_max;
  p->dt = dt;
  p->d_tau = d_tau;
  pid_set_gains(p, kp, ki, kd);
  pid_reset(p);
}

void pid_set_gains(pid_ctrl_t *p, float kp, float ki, float kd)
{
  p->kp = kp;
  p->ki = ki;
  p->kd = kd;
}

float pid_update(pid_ctrl_t *p, float setpoint, float measurement)
{
  if (!isfinite(setpoint) || !isfinite(measurement)) {
    return p->last_out;
  }

  float e = setpoint - measurement;

  float raw_d = p->primed ? (measurement - p->meas_prev) / p->dt : 0.0f;
  p->d_filt += (p->dt / (p->d_tau + p->dt)) * (raw_d - p->d_filt);
  p->meas_prev = measurement;
  p->primed = true;

  float integ_new = p->integ + p->ki * e * p->dt;
  float u = p->kp * e + integ_new - p->kd * p->d_filt;

  /* Clamping anti-windup: freeze the integrator while saturated and still pushing. */
  if (u > p->out_max) {
    u = p->out_max;
    if (e < 0.0f)
      p->integ = integ_new;
  } else if (u < p->out_min) {
    u = p->out_min;
    if (e > 0.0f)
      p->integ = integ_new;
  } else {
    p->integ = integ_new;
  }

  p->last_out = u;
  return u;
}
