#include <math.h>
#include <unity.h>

#include "motor_io_sim.h"
#include "motor_loop.h"
#include "motor_safety.h"

#define DT 1e-3f

static motor_safety_t safety;

void setUp(void)
{
  const motor_safety_cfg_t cfg = MOTOR_SAFETY_CFG_DEFAULT;
  motor_safety_init(&safety, &cfg);
}
void tearDown(void) {}

static void test_normal_speeds_are_fine(void)
{
  for (float rpm = -5000.0f; rpm <= 5000.0f; rpm += 100.0f) {
    TEST_ASSERT_EQUAL(MOTOR_TRIP_NONE, motor_safety_update(&safety, rpm));
  }
}

static void test_overspeed_latches_until_cleared(void)
{
  TEST_ASSERT_EQUAL(MOTOR_TRIP_OVERSPEED, motor_safety_update(&safety, 7001.0f));
  TEST_ASSERT_EQUAL(MOTOR_TRIP_OVERSPEED, motor_safety_update(&safety, 100.0f)); /* latched */
  motor_safety_clear(&safety);
  TEST_ASSERT_EQUAL(MOTOR_TRIP_NONE, motor_safety_update(&safety, 100.0f));
  TEST_ASSERT_EQUAL(MOTOR_TRIP_OVERSPEED, motor_safety_update(&safety, -7001.0f)); /* both ways */
}

static void test_sensor_timeout_needs_consecutive_bad_readings(void)
{
  for (int i = 0; i < 49; i++)
    TEST_ASSERT_EQUAL(MOTOR_TRIP_NONE, motor_safety_update(&safety, NAN));
  TEST_ASSERT_EQUAL(MOTOR_TRIP_NONE, motor_safety_update(&safety, 1000.0f)); /* a good one resets */
  for (int i = 0; i < 49; i++)
    TEST_ASSERT_EQUAL(MOTOR_TRIP_NONE, motor_safety_update(&safety, NAN));
  TEST_ASSERT_EQUAL(MOTOR_TRIP_SENSOR, motor_safety_update(&safety, NAN)); /* 50th in a row */
}

static void test_trip_names(void)
{
  TEST_ASSERT_EQUAL_STRING("none", motor_trip_name(MOTOR_TRIP_NONE));
  TEST_ASSERT_EQUAL_STRING("overspeed", motor_trip_name(MOTOR_TRIP_OVERSPEED));
  TEST_ASSERT_EQUAL_STRING("sensor timeout", motor_trip_name(MOTOR_TRIP_SENSOR));
}

/* Closed loop: a strong negative load torque drives the motor past the limit even with the
 * controller braking at full reverse duty; the loop must trip, stop and stay stopped. */
static void test_overspeed_in_closed_loop(void)
{
  const plant_params_t plant = PLANT_PARAMS_N20;
  const motor_ctrl_cfg_t ctrl = MOTOR_CTRL_CFG_DEFAULT;
  const motor_safety_cfg_t cfg = MOTOR_SAFETY_CFG_DEFAULT;
  motor_io_sim_t sim;
  motor_io_t io;
  motor_loop_t l;
  motor_io_sim_init(&sim, &io, &plant, DT);
  motor_loop_init(&l, &ctrl, &cfg, DT);
  motor_ctrl_set_setpoint(&l.ctrl, 3000.0f);
  TEST_ASSERT_TRUE(motor_loop_start(&l));

  int trip_tick = -1;
  for (int k = 0; k < 3000; k++) {
    if (k == 500)
      motor_io_sim_set_load(&sim, -0.06f);
    motor_loop_tick(&l, &io);
    if (trip_tick < 0 && !motor_safety_ok(&l.safety))
      trip_tick = k;
    motor_io_sim_step(&sim);
  }
  TEST_ASSERT_TRUE(trip_tick > 500);
  TEST_ASSERT_EQUAL(MOTOR_TRIP_OVERSPEED, l.safety.latched);
  TEST_ASSERT_FALSE(l.ctrl.run);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.duty);
}

/* A sensor that only returns NaN: the loop trips after the configured number of ticks. */
static float dead_sensor(void *ctx)
{
  (void)ctx;
  return NAN;
}
static int safe_calls;
static void count_safe(void *ctx)
{
  (void)ctx;
  safe_calls++;
}
static void ignore_duty(void *ctx, float duty) { (void)ctx, (void)duty; }

static void test_sensor_timeout_in_closed_loop(void)
{
  const motor_ctrl_cfg_t ctrl = MOTOR_CTRL_CFG_DEFAULT;
  const motor_safety_cfg_t cfg = MOTOR_SAFETY_CFG_DEFAULT;
  motor_io_t io = {.speed_read = dead_sensor, .duty_write = ignore_duty, .safe_state = count_safe};
  motor_loop_t l;
  motor_loop_init(&l, &ctrl, &cfg, DT);
  TEST_ASSERT_TRUE(motor_loop_start(&l));
  safe_calls = 0;
  int trip_tick = -1;
  for (int k = 0; k < 100; k++) {
    motor_loop_tick(&l, &io);
    if (trip_tick < 0 && !motor_safety_ok(&l.safety))
      trip_tick = k;
  }
  TEST_ASSERT_EQUAL(49, trip_tick); /* the 50th bad reading */
  TEST_ASSERT_EQUAL(MOTOR_TRIP_SENSOR, l.safety.latched);
  TEST_ASSERT_EQUAL(51, safe_calls); /* from the trip on, every tick */
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_normal_speeds_are_fine);
  RUN_TEST(test_overspeed_latches_until_cleared);
  RUN_TEST(test_sensor_timeout_needs_consecutive_bad_readings);
  RUN_TEST(test_trip_names);
  RUN_TEST(test_overspeed_in_closed_loop);
  RUN_TEST(test_sensor_timeout_in_closed_loop);
  return UNITY_END();
}
