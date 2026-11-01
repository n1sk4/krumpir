#ifndef MOTOR_IO_SIM_H
#define MOTOR_IO_SIM_H

#include "motor_io.h"
#include "plant_sim.h"

/* motor_io_t backend that talks to the simulated motor. A duty written in tick k is applied by
 * motor_io_sim_step(); the next read sees the result. */
typedef struct {
  plant_sim_t plant;
  float vbus;
  float duty;
  float load_torque; /* [N*m] */
} motor_io_sim_t;

void motor_io_sim_init(motor_io_sim_t *s, motor_io_t *io_out, const plant_params_t *p, float dt);
void motor_io_sim_step(motor_io_sim_t *s); /* advance the plant by one control period */
void motor_io_sim_set_load(motor_io_sim_t *s, float torque);

#endif
