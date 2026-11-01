#include "motor_ctrl.h"

void motor_ctrl_init(motor_ctrl_t *c, const motor_ctrl_cfg_t *cfg, float dt)
{
  c->cfg = *cfg;
  pid_init(&c->pid, cfg->kp, cfg->ki, cfg->kd, -1.0f, 1.0f, dt, 0.0f);
  c->setpoint = 0.0f;
  c->run = false;
}

void motor_ctrl_set_setpoint(motor_ctrl_t *c, float sp)
{
  c->setpoint = sp < c->cfg.sp_min ? c->cfg.sp_min : sp > c->cfg.sp_max ? c->cfg.sp_max : sp;
}

void motor_ctrl_set_gains(motor_ctrl_t *c, float kp, float ki, float kd)
{
  c->cfg.kp = kp;
  c->cfg.ki = ki;
  c->cfg.kd = kd;
  pid_set_gains(&c->pid, kp, ki, kd);
}

void motor_ctrl_start(motor_ctrl_t *c)
{
  pid_reset(&c->pid);
  c->run = true;
}

void motor_ctrl_stop(motor_ctrl_t *c)
{
  pid_reset(&c->pid);
  c->run = false;
}

float motor_ctrl_update(motor_ctrl_t *c, float speed_rpm)
{
  return c->run ? pid_update(&c->pid, c->setpoint, speed_rpm) : 0.0f;
}
