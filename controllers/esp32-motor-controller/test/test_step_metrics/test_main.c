#include <math.h>
#include <unity.h>

#include "step_metrics.h"

void setUp(void) {}
void tearDown(void) {}

static void test_first_order_response(void)
{
    const float tau = 0.05f, dt = 0.001f;
    step_metrics_t m;
    step_metrics_init(&m, 1000.0f, 2.0f, dt);
    for (int k = 1; k <= 1000; k++) step_metrics_add(&m, 1000.0f * (1.0f - expf(-k * dt / tau)));
    /* 2 % band is reached after -ln(0.02) * tau = 0.1956 s */
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.1956f, step_metrics_settling_s(&m));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, step_metrics_overshoot_pct(&m));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, step_metrics_final_error(&m));
}

static void test_overshoot_and_settling_index(void)
{
    step_metrics_t m;
    step_metrics_init(&m, 1.0f, 2.0f, 0.5f);
    const float v[] = {0.0f, 0.5f, 1.2f, 1.05f, 1.01f, 1.0f};
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) step_metrics_add(&m, v[i]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, step_metrics_overshoot_pct(&m));
    /* the last sample outside the band is index 3, so it settles at 4 * dt */
    TEST_ASSERT_EQUAL_FLOAT(2.0f, step_metrics_settling_s(&m));
}

static void test_dip_inside_and_outside_band(void)
{
    step_metrics_t m;
    step_metrics_init(&m, 100.0f, 2.0f, 0.001f);
    step_metrics_add(&m, 100.0f);
    step_metrics_add(&m, 99.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, step_metrics_settling_s(&m)); /* never left the band */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, step_metrics_undershoot_pct(&m));
    step_metrics_add(&m, 90.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, step_metrics_undershoot_pct(&m));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.003f, step_metrics_settling_s(&m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_order_response);
    RUN_TEST(test_overshoot_and_settling_index);
    RUN_TEST(test_dip_inside_and_outside_band);
    return UNITY_END();
}
