#include "t12_ctrl.h"

#include <math.h>

void t12_ctrl_init(t12_ctrl_t *c, const t12_ctrl_cfg_t *cfg, float dt)
{
  c->cfg = *cfg;
  pid_init(&c->pid, cfg->kp, cfg->ki, cfg->kd, 0.0f, 1.0f, dt, 0.0f);
  c->setpoint = cfg->sp_min;
  c->sp_active = cfg->sp_min;
  c->run = false;
}

void t12_ctrl_set_setpoint(t12_ctrl_t *c, float sp)
{
  c->setpoint = sp < c->cfg.sp_min ? c->cfg.sp_min : sp > c->cfg.sp_max ? c->cfg.sp_max : sp;
}

void t12_ctrl_set_gains(t12_ctrl_t *c, float kp, float ki, float kd)
{
  c->cfg.kp = kp;
  c->cfg.ki = ki;
  c->cfg.kd = kd;
  pid_set_gains(&c->pid, kp, ki, kd);
}

void t12_ctrl_start(t12_ctrl_t *c, float temp)
{
  pid_reset(&c->pid);
  bool ramped = c->cfg.ramp > 0.0f && isfinite(temp) && temp < c->setpoint;
  c->sp_active = ramped ? temp : c->setpoint;
  c->run = true;
}

void t12_ctrl_stop(t12_ctrl_t *c)
{
  pid_reset(&c->pid);
  c->run = false;
}

float t12_ctrl_update(t12_ctrl_t *c, float temp)
{
  if (!c->run)
    return 0.0f;
  float step = c->cfg.ramp * c->pid.dt;
  if (step > 0.0f && c->sp_active != c->setpoint) {
    float diff = c->setpoint - c->sp_active;
    c->sp_active = fabsf(diff) <= step ? c->setpoint : c->sp_active + copysignf(step, diff);
  } else {
    c->sp_active = c->setpoint;
  }
  return pid_update(&c->pid, c->sp_active, temp);
}
