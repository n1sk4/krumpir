#include "io_sim.h"

#define RAD_S_TO_RPM (60.0f / 6.28318530718f)

static float sensor_read(void *ctx)
{
    return ((io_sim_t *)ctx)->plant.w * RAD_S_TO_RPM;
}

static void actuator_write(void *ctx, float duty)
{
    io_sim_t *s = ctx;
    s->duty = duty > 1.0f ? 1.0f : (duty < -1.0f ? -1.0f : duty);
}

static void safe_state(void *ctx)
{
    ((io_sim_t *)ctx)->duty = 0.0f;
}

void io_sim_init(io_sim_t *s, io_t *io_out, const plant_params_t *p, float dt)
{
    plant_sim_init(&s->plant, p, dt);
    s->vbus = p->Vbus;
    s->duty = 0.0f;
    s->load_torque = 0.0f;
    io_out->sensor_read = sensor_read;
    io_out->actuator_write = actuator_write;
    io_out->safe_state = safe_state;
    io_out->ctx = s;
}

void io_sim_step(io_sim_t *s)
{
    plant_sim_step(&s->plant, s->duty * s->vbus, s->load_torque);
}

void io_sim_set_load(io_sim_t *s, float torque)
{
    s->load_torque = torque;
}
