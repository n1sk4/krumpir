#include <unity.h>

#include "motor_ctrl.h"

#define DT 1e-3f

static motor_ctrl_t c;

void setUp(void)
{
  const motor_ctrl_cfg_t cfg = MOTOR_CTRL_CFG_DEFAULT;
  motor_ctrl_init(&c, &cfg, DT);
}
void tearDown(void) {}

static void test_stopped_gives_zero(void)
{
  motor_ctrl_set_setpoint(&c, 3000.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motor_ctrl_update(&c, 0.0f));
}

static void test_setpoint_is_clamped(void)
{
  motor_ctrl_set_setpoint(&c, 20000.0f);
  TEST_ASSERT_EQUAL_FLOAT(10000.0f, c.setpoint);
  motor_ctrl_set_setpoint(&c, -20000.0f);
  TEST_ASSERT_EQUAL_FLOAT(-10000.0f, c.setpoint);
}

static void test_runs_and_restarts_from_zero(void)
{
  motor_ctrl_set_setpoint(&c, 3000.0f);
  motor_ctrl_start(&c);
  float first = motor_ctrl_update(&c, 0.0f);
  TEST_ASSERT_TRUE(first > 0.0f);
  for (int k = 0; k < 100; k++)
    motor_ctrl_update(&c, 0.0f); /* the integrator grows */
  motor_ctrl_stop(&c);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motor_ctrl_update(&c, 0.0f));
  motor_ctrl_start(&c);
  TEST_ASSERT_EQUAL_FLOAT(first, motor_ctrl_update(&c, 0.0f)); /* integrator reset */
}

static void test_gains_change_without_reset(void)
{
  motor_ctrl_set_setpoint(&c, 1000.0f);
  motor_ctrl_start(&c);
  motor_ctrl_set_gains(&c, 0.0f, 0.0f, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, motor_ctrl_update(&c, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.cfg.kp);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_stopped_gives_zero);
  RUN_TEST(test_setpoint_is_clamped);
  RUN_TEST(test_runs_and_restarts_from_zero);
  RUN_TEST(test_gains_change_without_reset);
  return UNITY_END();
}
