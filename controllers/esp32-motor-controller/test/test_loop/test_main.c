#include <math.h>
#include <unity.h>

#include "motor_loop.h"

#define DT 1e-3f

/* A fake board: fixed speed, records what the loop does to the driver. */
static float speed;
static float written;
static int safe_calls;
static float fake_speed(void *ctx) { return (void)ctx, speed; }
static void fake_duty(void *ctx, float d) { (void)ctx, written = d; }
static void fake_safe(void *ctx) { (void)ctx, safe_calls++; }
static const motor_io_t io = {fake_speed, fake_duty, fake_safe, NULL};

static motor_loop_t l;

void setUp(void)
{
  const motor_ctrl_cfg_t ctrl = MOTOR_CTRL_CFG_DEFAULT;
  const motor_safety_cfg_t safety = MOTOR_SAFETY_CFG_DEFAULT;
  motor_loop_init(&l, &ctrl, &safety, DT);
  speed = 0.0f;
  written = NAN;
  safe_calls = 0;
}
void tearDown(void) {}

static void test_stopped_holds_the_safe_state(void)
{
  motor_loop_tick(&l, &io);
  TEST_ASSERT_EQUAL(1, safe_calls);
  TEST_ASSERT_TRUE(isnan(written));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, l.duty);
}

static void test_running_drives_the_motor(void)
{
  motor_ctrl_set_setpoint(&l.ctrl, 3000.0f);
  TEST_ASSERT_TRUE(motor_loop_start(&l));
  motor_loop_tick(&l, &io);
  TEST_ASSERT_EQUAL(0, safe_calls);
  TEST_ASSERT_TRUE(written > 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(written, l.duty);
}

static void test_trip_stops_and_refuses_start_until_cleared(void)
{
  motor_ctrl_set_setpoint(&l.ctrl, 3000.0f);
  TEST_ASSERT_TRUE(motor_loop_start(&l));
  speed = 8000.0f; /* overspeed */
  motor_loop_tick(&l, &io);
  TEST_ASSERT_FALSE(l.ctrl.run);
  TEST_ASSERT_EQUAL(1, safe_calls);
  speed = 0.0f;
  TEST_ASSERT_FALSE(motor_loop_start(&l)); /* latched */
  motor_loop_clear(&l);
  TEST_ASSERT_TRUE(motor_loop_start(&l));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_stopped_holds_the_safe_state);
  RUN_TEST(test_running_drives_the_motor);
  RUN_TEST(test_trip_stops_and_refuses_start_until_cleared);
  return UNITY_END();
}
