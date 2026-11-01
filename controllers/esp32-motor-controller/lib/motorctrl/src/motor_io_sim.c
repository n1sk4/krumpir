#include "motor_io_sim.h"

#define RAD_S_TO_RPM (60.0f / 6.28318530718f)

static float speed_read(void *ctx) { return ((motor_io_sim_t *)ctx)->plant.w * RAD_S_TO_RPM; }

static void duty_write(void *ctx, float duty)
{
  motor_io_sim_t *s = ctx;
  s->duty = duty > 1.0f ? 1.0f : (duty < -1.0f ? -1.0f : duty);
}

static void safe_state(void *ctx) { ((motor_io_sim_t *)ctx)->duty = 0.0f; }

void motor_io_sim_init(motor_io_sim_t *s, motor_io_t *io_out, const plant_params_t *p, float dt)
{
  plant_sim_init(&s->plant, p, dt);
  s->vbus = p->Vbus;
  s->duty = 0.0f;
  s->load_torque = 0.0f;
  io_out->speed_read = speed_read;
  io_out->duty_write = duty_write;
  io_out->safe_state = safe_state;
  io_out->ctx = s;
}

void motor_io_sim_step(motor_io_sim_t *s)
{
  plant_sim_step(&s->plant, s->duty * s->vbus, s->load_torque);
}

void motor_io_sim_set_load(motor_io_sim_t *s, float torque) { s->load_torque = torque; }
