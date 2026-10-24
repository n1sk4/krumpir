#include <unity.h>

#include "loop_stats.h"

void setUp(void) {}
void tearDown(void) {}

static void test_min_max_mean(void)
{
    loop_stats_t s;
    loop_stats_reset(&s);
    loop_stats_add(&s, 10);
    loop_stats_add(&s, 30);
    loop_stats_add(&s, 20);
    TEST_ASSERT_EQUAL_UINT32(10, s.min);
    TEST_ASSERT_EQUAL_UINT32(30, s.max);
    TEST_ASSERT_EQUAL_UINT32(3, s.count);
    TEST_ASSERT_EQUAL_UINT32(20, loop_stats_mean(&s));
}

static void test_empty_and_reset(void)
{
    loop_stats_t s;
    loop_stats_reset(&s);
    TEST_ASSERT_EQUAL_UINT32(0, loop_stats_mean(&s));
    loop_stats_add(&s, 5);
    loop_stats_reset(&s);
    TEST_ASSERT_EQUAL_UINT32(0, s.count);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.min);
}

static void test_sum_does_not_overflow(void)
{
    loop_stats_t s;
    loop_stats_reset(&s);
    loop_stats_add(&s, 4000000000u);
    loop_stats_add(&s, 4000000000u);
    TEST_ASSERT_EQUAL_UINT32(4000000000u, loop_stats_mean(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_min_max_mean);
    RUN_TEST(test_empty_and_reset);
    RUN_TEST(test_sum_does_not_overflow);
    return UNITY_END();
}
