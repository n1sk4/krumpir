#include <unity.h>

#include "rte_calib.h"

static void set_param(void *ctx, int id, float v) { (void)ctx, (void)id, (void)v; }
static bool start(void *ctx) { return (void)ctx, true; }
static void nop(void *ctx) { (void)ctx; }
static const char *trip(void *ctx) { return (void)ctx, (const char *)0; }

static const rte_param_t params[] = {
    {"sp", "", "", 100.0f, 450.0f, 350.0f},
    {"kp", "", "", 0.0f, 1000.0f, 0.05f},
};
static const rte_hooks_t hooks = {set_param, start, nop, nop, trip, NULL};
static const char *const page[] = {"sp", "kp", "nope"};
static rte_t r;
static rte_calib_t c;

void setUp(void)
{
  rte_init(&r, params, 2, &hooks);
  rte_calib_init(&c, &r, page, 3);
}
void tearDown(void) {}

static void test_first_page_only_records(void)
{
  const float v[] = {300.0f, 1.0f, 0.0f};
  TEST_ASSERT_FALSE(rte_calib_update(&c, &r, v));
  TEST_ASSERT_EQUAL_FLOAT(350.0f, rte_get(&r, 0)); /* the console's values stand */
  TEST_ASSERT_FALSE(rte_calib_update(&c, &r, v)); /* unchanged */
}

static void test_change_writes_values_in_range(void)
{
  const float a[] = {350.0f, 0.05f, 0.0f};
  const float b[] = {300.0f, 2000.0f, 9.0f}; /* kp out of range, third has no parameter */
  rte_calib_update(&c, &r, a);
  TEST_ASSERT_TRUE(rte_calib_update(&c, &r, b));
  TEST_ASSERT_EQUAL_FLOAT(300.0f, rte_get(&r, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.05f, rte_get(&r, 1)); /* ignored */
  TEST_ASSERT_EQUAL(-1, c.ids[2]);
}

static void test_request_runs_each_new_value_once(void)
{
  TEST_ASSERT_EQUAL(0, rte_calib_request(&c, &r, 0x0101u)); /* left on the page: only recorded */
  TEST_ASSERT_FALSE(rte_running(&r));
  TEST_ASSERT_EQUAL(RTE_REQ_START, rte_calib_request(&c, &r, 0x0201u));
  TEST_ASSERT_TRUE(rte_running(&r));
  TEST_ASSERT_EQUAL(0, rte_calib_request(&c, &r, 0x0201u)); /* unchanged */
  TEST_ASSERT_EQUAL(RTE_REQ_STOP, rte_calib_request(&c, &r, 0x0302u));
  TEST_ASSERT_FALSE(rte_running(&r));
  TEST_ASSERT_EQUAL(RTE_REQ_START, rte_calib_request(&c, &r, 0x0401u)); /* again, new sequence */
  TEST_ASSERT_TRUE(rte_running(&r));
  TEST_ASSERT_EQUAL(RTE_REQ_CLEAR, rte_calib_request(&c, &r, 0x0503u));
  TEST_ASSERT_TRUE(r.clear_req);
  TEST_ASSERT_EQUAL(0, rte_calib_request(&c, &r, 0x0609u)); /* unknown command */
  TEST_ASSERT_TRUE(rte_running(&r));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_first_page_only_records);
  RUN_TEST(test_change_writes_values_in_range);
  RUN_TEST(test_request_runs_each_new_value_once);
  return UNITY_END();
}
