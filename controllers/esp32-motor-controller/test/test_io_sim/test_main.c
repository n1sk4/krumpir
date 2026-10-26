#include <math.h>
#include <unity.h>

#include "ctrl_params.h"
#include "io_sim.h"
#include "pid.h"

#define DT 1e-3f

static const plant_params_t P = PLANT_PARAMS_N20;

void setUp(void) {}
void tearDown(void) {}

static void test_rpm_clamp_and_safe_state(void)
{
  io_sim_t sim;
  io_t io;
  io_sim_init(&sim, &io, &P, DT);
  sim.plant.w = 100.0f; /* rad/s */
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 954.93f, io.sensor_read(io.ctx));
  io.actuator_write(io.ctx, 5.0f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, sim.duty);
  io.actuator_write(io.ctx, -5.0f);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, sim.duty);
  io.safe_state(io.ctx);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.duty);
}

static void loop_init(io_sim_t *sim, io_t *io, pid_ctrl_t *pid)
{
  io_sim_init(sim, io, &P, DT);
  const ctrl_params_t gains = CTRL_PARAMS_DEFAULT;
  pid_init(pid, gains.kp, gains.ki, gains.kd, -1.0f, 1.0f, DT, 0.0f);
}

static float run_loop(io_sim_t *sim, io_t *io, pid_ctrl_t *pid, float sp, int steps, float *min_rpm)
{
  for (int k = 0; k < steps; k++) {
    float rpm = io->sensor_read(io->ctx);
    TEST_ASSERT_TRUE(isfinite(rpm));
    float duty = pid_update(pid, sp, rpm);
    TEST_ASSERT_TRUE(duty >= -1.0f && duty <= 1.0f);
    io->actuator_write(io->ctx, duty);
    io_sim_step(sim);
    if (min_rpm && rpm < *min_rpm)
      *min_rpm = rpm;
  }
  return io->sensor_read(io->ctx);
}

static void test_closed_loop_step(void)
{
  io_sim_t sim;
  io_t io;
  pid_ctrl_t pid;
  loop_init(&sim, &io, &pid);
  float rpm = run_loop(&sim, &io, &pid, 3000.0f, 1000, NULL);
  TEST_ASSERT_FLOAT_WITHIN(60.0f, 3000.0f, rpm); /* within 2 % */
}

static void test_saturation_recovers(void)
{
  io_sim_t sim;
  io_t io;
  pid_ctrl_t pid;
  loop_init(&sim, &io, &pid);
  run_loop(&sim, &io, &pid, 10000.0f, 1000, NULL); /* above no-load speed: duty pinned at 1 */
  float min_rpm = 1e9f;
  float rpm = run_loop(&sim, &io, &pid, 3000.0f, 1000, &min_rpm);
  TEST_ASSERT_FLOAT_WITHIN(90.0f, 3000.0f, rpm);
  TEST_ASSERT_TRUE(min_rpm > 1500.0f); /* windup would drag this toward 0 */
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_rpm_clamp_and_safe_state);
  RUN_TEST(test_closed_loop_step);
  RUN_TEST(test_saturation_recovers);
  return UNITY_END();
}
