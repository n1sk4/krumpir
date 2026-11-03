#include "io_hil.h"

#include <math.h>

static float speed_read(void *ctx)
{
  const motor_io_hil_t *h = ctx;
  return h->slots.age <= HIL_MAX_AGE ? h->slots.plant[0] : NAN;
}

static void duty_write(void *ctx, float duty) { ((motor_io_hil_t *)ctx)->slots.cmd[0] = duty; }

static void safe_state(void *ctx) { ((motor_io_hil_t *)ctx)->slots.cmd[0] = 0.0f; }

void motor_io_hil_init(motor_io_hil_t *h, motor_io_t *io_out)
{
  h->slots.cmd[0] = 0.0f;
  io_out->speed_read = speed_read;
  io_out->duty_write = duty_write;
  io_out->safe_state = safe_state;
  io_out->ctx = h;
}
