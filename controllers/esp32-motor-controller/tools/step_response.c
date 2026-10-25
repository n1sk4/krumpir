/* Host program: PID + simulated motor, 3000 rpm setpoint step. Prints CSV to stdout.
 * Build/run: see README ("Step response"). */
#include <stdio.h>

#include "ctrl_params.h"
#include "io_sim.h"
#include "pid.h"

#define DT 1e-3f
#define STEPS 1000
#define STEP_AT 100
#define SETPOINT_RPM 3000.0f

int main(void)
{
    plant_params_t p = PLANT_PARAMS_N20;
    io_sim_t sim;
    io_t io;
    pid_ctrl_t pid;
    io_sim_init(&sim, &io, &p, DT);
    const ctrl_params_t gains = CTRL_PARAMS_DEFAULT;
    pid_init(&pid, gains.kp, gains.ki, gains.kd, -1.0f, 1.0f, DT, 0.0f);

    printf("t_s,setpoint_rpm,speed_rpm,duty,error_rpm\n");
    for (int k = 0; k < STEPS; k++) {
        float sp = k >= STEP_AT ? SETPOINT_RPM : 0.0f;
        float rpm = io.sensor_read(io.ctx);
        float duty = pid_update(&pid, sp, rpm);
        io.actuator_write(io.ctx, duty);
        printf("%.3f,%.1f,%.1f,%.4f,%.1f\n", k * DT, sp, rpm, sim.duty, sp - rpm);
        io_sim_step(&sim);
    }
    return 0;
}
