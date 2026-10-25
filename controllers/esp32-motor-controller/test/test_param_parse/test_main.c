#include <unity.h>

#include "param_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void test_valid(void)
{
    float v = 0.0f;
    TEST_ASSERT_TRUE(parse_float("1500", -10000.0f, 10000.0f, &v));
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, v);
    TEST_ASSERT_TRUE(parse_float("-2.5e1", -100.0f, 100.0f, &v));
    TEST_ASSERT_EQUAL_FLOAT(-25.0f, v);
}

static void test_range(void)
{
    float v = 7.0f;
    TEST_ASSERT_FALSE(parse_float("-1", 0.0f, 10.0f, &v));
    TEST_ASSERT_FALSE(parse_float("11", 0.0f, 10.0f, &v));
    TEST_ASSERT_TRUE(parse_float("10", 0.0f, 10.0f, &v)); /* bounds are inclusive */
    TEST_ASSERT_EQUAL_FLOAT(10.0f, v);
}

static void test_rejects_garbage_and_non_finite(void)
{
    float v = 7.0f;
    TEST_ASSERT_FALSE(parse_float("", 0.0f, 10.0f, &v));
    TEST_ASSERT_FALSE(parse_float(NULL, 0.0f, 10.0f, &v));
    TEST_ASSERT_FALSE(parse_float("abc", 0.0f, 10.0f, &v));
    TEST_ASSERT_FALSE(parse_float("5x", 0.0f, 10.0f, &v));
    TEST_ASSERT_FALSE(parse_float("nan", 0.0f, 10.0f, &v));
    TEST_ASSERT_FALSE(parse_float("inf", 0.0f, 1e30f, &v));
    TEST_ASSERT_EQUAL_FLOAT(7.0f, v); /* output untouched on failure */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid);
    RUN_TEST(test_range);
    RUN_TEST(test_rejects_garbage_and_non_finite);
    return UNITY_END();
}
