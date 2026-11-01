/* Host program: PID + simulated motor, 3000 rpm setpoint step. Prints CSV to stdout.
 * Build/run: see README ("Step response"). */
#include <stdio.h>

#include "motor_io_sim.h"
#include "motor_loop.h"

#define DT 1e-3f
#define STEPS 1000
#define STEP_AT 100
#define SETPOINT_RPM 3000.0f

int main(void)
{
  plant_params_t p = PLANT_PARAMS_N20;
  const motor_ctrl_cfg_t ctrl = MOTOR_CTRL_CFG_DEFAULT;
  const motor_safety_cfg_t safety = MOTOR_SAFETY_CFG_DEFAULT;
  motor_io_sim_t sim;
  motor_io_t io;
  motor_loop_t loop;
  motor_io_sim_init(&sim, &io, &p, DT);
  motor_loop_init(&loop, &ctrl, &safety, DT);
  motor_loop_start(&loop);

  printf("t_s,setpoint_rpm,speed_rpm,duty,error_rpm\n");
  for (int k = 0; k < STEPS; k++) {
    float sp = k >= STEP_AT ? SETPOINT_RPM : 0.0f;
    motor_ctrl_set_setpoint(&loop.ctrl, sp);
    motor_loop_tick(&loop, &io);
    float rpm = loop.speed;
    printf("%.3f,%.1f,%.1f,%.4f,%.1f\n", k * DT, sp, rpm, sim.duty, sp - rpm);
    motor_io_sim_step(&sim);
  }
  return 0;
}
