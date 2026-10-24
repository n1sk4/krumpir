#include <math.h>
#include <unity.h>

#include "pid.h"

#define DT 0.001f

static pid_ctrl_t pid;

void setUp(void) {}
void tearDown(void) {}

static void test_p_only(void)
{
    pid_init(&pid, 2.0f, 0.0f, 0.0f, -10.0f, 10.0f, DT, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.0f, pid_update(&pid, 5.0f, 2.0f));
}

static void test_integrator_accumulates(void)
{
    pid_init(&pid, 0.0f, 2.0f, 0.0f, -10.0f, 10.0f, DT, 0.0f);
    float u = 0.0f;
    for (int i = 0; i < 100; i++) u = pid_update(&pid, 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f * 1.0f * DT * 100, u);
}

static void test_output_clamps(void)
{
    pid_init(&pid, 100.0f, 0.0f, 0.0f, -1.0f, 1.0f, DT, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pid_update(&pid, 10.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, pid_update(&pid, -10.0f, 0.0f));
}

static void test_anti_windup_recovers(void)
{
    pid_init(&pid, 1.0f, 100.0f, 0.0f, -1.0f, 1.0f, DT, 0.0f);
    for (int i = 0; i < 5000; i++) pid_update(&pid, 10.0f, 0.0f); /* saturated high */
    /* Error flips to -0.5: P term alone is -0.5, integrator must not hold it up. */
    float u = pid_update(&pid, 0.0f, 0.5f);
    TEST_ASSERT_TRUE(u < 1.0f);
    int steps = 0;
    while (pid_update(&pid, 0.0f, 0.5f) > 0.0f && steps < 100) steps++;
    TEST_ASSERT_TRUE(steps < 20);
}

static void test_no_derivative_kick(void)
{
    pid_init(&pid, 0.0f, 0.0f, 1.0f, -100.0f, 100.0f, DT, 0.0f);
    pid_update(&pid, 0.0f, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid_update(&pid, 100.0f, 5.0f));
}

static void test_derivative_on_measurement(void)
{
    pid_init(&pid, 0.0f, 0.0f, 0.01f, -100.0f, 100.0f, DT, 0.0f);
    float meas = 0.0f, u = 0.0f;
    for (int i = 0; i < 10; i++) {
        meas += 0.1f; /* slope 100 per second */
        u = pid_update(&pid, 0.0f, meas);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -1.0f, u);
}

static void test_reset(void)
{
    pid_init(&pid, 0.0f, 10.0f, 1.0f, -100.0f, 100.0f, DT, 0.0f);
    for (int i = 0; i < 50; i++) pid_update(&pid, 1.0f, 0.0f);
    pid_reset(&pid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.integ);
    /* First update after reset: measurement jump must not produce a D spike. */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, pid_update(&pid, 50.0f, 50.0f));
}

static void test_nan_ignored(void)
{
    pid_init(&pid, 1.0f, 0.0f, 0.0f, -10.0f, 10.0f, DT, 0.0f);
    float good = pid_update(&pid, 3.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(good, pid_update(&pid, 3.0f, NAN));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, pid_update(&pid, 3.0f, 1.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_p_only);
    RUN_TEST(test_integrator_accumulates);
    RUN_TEST(test_output_clamps);
    RUN_TEST(test_anti_windup_recovers);
    RUN_TEST(test_no_derivative_kick);
    RUN_TEST(test_derivative_on_measurement);
    RUN_TEST(test_reset);
    RUN_TEST(test_nan_ignored);
    return UNITY_END();
}
