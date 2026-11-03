#include "motor_loop.h"

void motor_loop_init(motor_loop_t *l, const motor_ctrl_cfg_t *ctrl,
                     const motor_safety_cfg_t *safety, float tick_dt_s)
{
  motor_ctrl_init(&l->ctrl, ctrl, tick_dt_s);
  motor_safety_init(&l->safety, safety);
  l->speed = 0.0f;
  l->duty = 0.0f;
}

void motor_loop_tick(motor_loop_t *l, const motor_io_t *io)
{
  l->speed = io->speed_read(io->ctx);
  if (motor_safety_update(&l->safety, l->speed) != MOTOR_TRIP_NONE && l->ctrl.run)
    motor_ctrl_stop(&l->ctrl);
  if (l->ctrl.run) {
    l->duty = motor_ctrl_update(&l->ctrl, l->speed);
    io->duty_write(io->ctx, l->duty);
  } else {
    l->duty = 0.0f;
    io->safe_state(io->ctx); /* the motor coasts */
  }
}

bool motor_loop_start(motor_loop_t *l)
{
  if (!motor_safety_ok(&l->safety))
    return false;
  motor_ctrl_start(&l->ctrl);
  return true;
}

void motor_loop_stop(motor_loop_t *l) { motor_ctrl_stop(&l->ctrl); }

void motor_loop_clear(motor_loop_t *l) { motor_safety_clear(&l->safety); }
