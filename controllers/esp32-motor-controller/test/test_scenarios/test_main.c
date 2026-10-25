#include <unity.h>

#include "scenario.h"

static ctrl_params_t gains = CTRL_PARAMS_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

/* Targets: overshoot < 10 %, settling < 0.3 s, no steady-state error. */
static void check_step_targets(scn_result_t r)
{
    TEST_ASSERT_TRUE(r.overshoot_pct < 10.0f);
    TEST_ASSERT_TRUE(r.settling_s < 0.3f);
    TEST_ASSERT_FLOAT_WITHIN(15.0f, 0.0f, r.final_error_rpm); /* 0.5 % of 3000 rpm */
}

static void test_setpoint_step(void)
{
    check_step_targets(scenario_run(SCN_STEP, &gains, NULL, NULL));
}

static void test_load_torque_step(void)
{
    scn_result_t r = scenario_run(SCN_LOAD_STEP, &gains, NULL, NULL);
    TEST_ASSERT_TRUE(r.undershoot_pct < 5.0f); /* speed dip when the load arrives */
    TEST_ASSERT_TRUE(r.settling_s < 0.3f);
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 0.0f, r.final_error_rpm); /* the integrator removes it */
}

static void test_saturation_recovers_without_windup(void)
{
    scn_result_t r = scenario_run(SCN_SATURATION, &gains, NULL, NULL);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.duty_saturated); /* output pinned at the limit */
    TEST_ASSERT_TRUE(r.settling_s < 0.5f);
    TEST_ASSERT_TRUE(r.min_rpm > 0.6f * 3000.0f); /* windup would drag the speed far below */
    TEST_ASSERT_FLOAT_WITHIN(15.0f, 0.0f, r.final_error_rpm);
}

static void test_double_inertia(void)
{
    check_step_targets(scenario_run(SCN_HEAVY_INERTIA, &gains, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_setpoint_step);
    RUN_TEST(test_load_torque_step);
    RUN_TEST(test_saturation_recovers_without_windup);
    RUN_TEST(test_double_inertia);
    return UNITY_END();
}
