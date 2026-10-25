#include <math.h>
#include <unity.h>

#include "ctrl_params.h"
#include "io_sim.h"
#include "pid.h"
#include "safety.h"

#define DT 1e-3f

static safety_t safety;

void setUp(void)
{
    const safety_cfg_t cfg = SAFETY_CFG_DEFAULT;
    safety_init(&safety, &cfg);
}
void tearDown(void) {}

static void test_normal_speeds_are_fine(void)
{
    for (float rpm = -5000.0f; rpm <= 5000.0f; rpm += 100.0f) {
        TEST_ASSERT_EQUAL(FAULT_NONE, safety_update(&safety, rpm));
    }
}

static void test_overspeed_latches_until_cleared(void)
{
    TEST_ASSERT_EQUAL(FAULT_OVERSPEED, safety_update(&safety, 7001.0f));
    TEST_ASSERT_EQUAL(FAULT_OVERSPEED, safety_update(&safety, 100.0f)); /* stays latched */
    safety_clear(&safety);
    TEST_ASSERT_EQUAL(FAULT_NONE, safety_update(&safety, 100.0f));
    TEST_ASSERT_EQUAL(FAULT_OVERSPEED, safety_update(&safety, -7001.0f)); /* both directions */
}

static void test_sensor_timeout_needs_consecutive_bad_readings(void)
{
    for (int i = 0; i < 49; i++) TEST_ASSERT_EQUAL(FAULT_NONE, safety_update(&safety, NAN));
    TEST_ASSERT_EQUAL(FAULT_NONE, safety_update(&safety, 1000.0f)); /* a good reading resets */
    for (int i = 0; i < 49; i++) TEST_ASSERT_EQUAL(FAULT_NONE, safety_update(&safety, NAN));
    TEST_ASSERT_EQUAL(FAULT_SENSOR_TIMEOUT, safety_update(&safety, NAN)); /* 50th in a row */
}

static void test_fault_names(void)
{
    TEST_ASSERT_EQUAL_STRING("none", fault_name(FAULT_NONE));
    TEST_ASSERT_EQUAL_STRING("overspeed", fault_name(FAULT_OVERSPEED));
    TEST_ASSERT_EQUAL_STRING("sensor timeout", fault_name(FAULT_SENSOR_TIMEOUT));
}

/* Closed loop: a strong negative load torque drives the motor past the limit even with the
 * controller braking at full reverse duty; the supervisor must stop it and keep it stopped. */
static void test_overspeed_in_closed_loop(void)
{
    const plant_params_t plant = PLANT_PARAMS_N20;
    const ctrl_params_t gains = CTRL_PARAMS_DEFAULT;
    io_sim_t sim;
    io_t io;
    pid_ctrl_t pid;
    io_sim_init(&sim, &io, &plant, DT);
    pid_init(&pid, gains.kp, gains.ki, gains.kd, -1.0f, 1.0f, DT, 0.0f);

    int fault_tick = -1;
    for (int k = 0; k < 3000; k++) {
        if (k == 500) io_sim_set_load(&sim, -0.06f);
        float rpm = io.sensor_read(io.ctx);
        if (safety_update(&safety, rpm) != FAULT_NONE) {
            io.safe_state(io.ctx);
            if (fault_tick < 0) fault_tick = k;
        } else {
            io.actuator_write(io.ctx, pid_update(&pid, 3000.0f, rpm));
        }
        io_sim_step(&sim);
    }
    TEST_ASSERT_TRUE(fault_tick > 500);
    TEST_ASSERT_EQUAL(FAULT_OVERSPEED, safety.latched);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sim.duty);
}

/* A sensor that only returns NaN: the supervisor trips after the configured number of ticks. */
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

static void test_sensor_timeout_in_closed_loop(void)
{
    io_t io = {.sensor_read = dead_sensor, .safe_state = count_safe};
    safe_calls = 0;
    int fault_tick = -1;
    for (int k = 0; k < 100; k++) {
        if (safety_update(&safety, io.sensor_read(io.ctx)) != FAULT_NONE) {
            io.safe_state(io.ctx);
            if (fault_tick < 0) fault_tick = k;
        }
    }
    TEST_ASSERT_EQUAL(49, fault_tick); /* the 50th bad reading */
    TEST_ASSERT_EQUAL(FAULT_SENSOR_TIMEOUT, safety.latched);
    TEST_ASSERT_EQUAL(51, safe_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_speeds_are_fine);
    RUN_TEST(test_overspeed_latches_until_cleared);
    RUN_TEST(test_sensor_timeout_needs_consecutive_bad_readings);
    RUN_TEST(test_fault_names);
    RUN_TEST(test_overspeed_in_closed_loop);
    RUN_TEST(test_sensor_timeout_in_closed_loop);
    return UNITY_END();
}
