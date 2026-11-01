#include <math.h>
#include <unity.h>

#include "motor_io_sim.h"
#include "motor_loop.h"

#define DT 1e-3f

static const plant_params_t P = PLANT_PARAMS_N20;
static const motor_ctrl_cfg_t CTRL = MOTOR_CTRL_CFG_DEFAULT;
static const motor_safety_cfg_t SAFETY = MOTOR_SAFETY_CFG_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

static void test_rpm_clamp_and_safe_state(void)
{
  motor_io_sim_t sim;
  motor_io_t io;
  motor_io_sim_init(&sim, &io, &P, DT);
  sim.plant.w = 100.0f; /* rad/s */
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 954.93f, io.speed_read(io.ctx));
  io.duty_write(io.ctx, 5.0f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, sim.duty);
  io.duty_write(io.ctx, -5.0f);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, sim.duty);
  io.safe_state(io.ctx);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.duty);
}

static void loop_init(motor_io_sim_t *sim, motor_io_t *io, motor_loop_t *l)
{
  motor_io_sim_init(sim, io, &P, DT);
  motor_loop_init(l, &CTRL, &SAFETY, DT);
  TEST_ASSERT_TRUE(motor_loop_start(l));
}

static float run_loop(motor_io_sim_t *sim, motor_io_t *io, motor_loop_t *l, float sp, int steps,
                      float *min_rpm)
{
  motor_ctrl_set_setpoint(&l->ctrl, sp);
  for (int k = 0; k < steps; k++) {
    motor_loop_tick(l, io);
    TEST_ASSERT_TRUE(isfinite(l->speed));
    TEST_ASSERT_TRUE(l->duty >= -1.0f && l->duty <= 1.0f);
    motor_io_sim_step(sim);
    if (min_rpm && l->speed < *min_rpm)
      *min_rpm = l->speed;
  }
  return io->speed_read(io->ctx);
}

static void test_closed_loop_step(void)
{
  motor_io_sim_t sim;
  motor_io_t io;
  motor_loop_t l;
  loop_init(&sim, &io, &l);
  float rpm = run_loop(&sim, &io, &l, 3000.0f, 1000, NULL);
  TEST_ASSERT_FLOAT_WITHIN(60.0f, 3000.0f, rpm); /* within 2 % */
}

static void test_saturation_recovers(void)
{
  motor_io_sim_t sim;
  motor_io_t io;
  motor_loop_t l;
  loop_init(&sim, &io, &l);
  run_loop(&sim, &io, &l, 10000.0f, 1000, NULL); /* above no-load speed: duty pinned at 1 */
  float min_rpm = 1e9f;
  float rpm = run_loop(&sim, &io, &l, 3000.0f, 1000, &min_rpm);
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
