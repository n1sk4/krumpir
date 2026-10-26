#ifndef IO_SIM_H
#define IO_SIM_H

#include "io.h"
#include "plant_sim.h"

/* io_t backend that talks to the simulated motor. The controller sees rpm and duty (-1..+1).
 * A duty written in cycle k is applied by io_sim_step(); the next read sees the result. */
typedef struct {
  plant_sim_t plant;
  float vbus;
  float duty;
  float load_torque; /* [N*m] */
} io_sim_t;

void io_sim_init(io_sim_t *s, io_t *io_out, const plant_params_t *p, float dt);
void io_sim_step(io_sim_t *s); /* advance the plant by one control period */
void io_sim_set_load(io_sim_t *s, float torque);

#endif
