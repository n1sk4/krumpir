#include <unity.h>

#include "console_host.h"
#include "console_util.h"
#include "rte_cmd.h"

static const char *trip_;
static void set_param(void *ctx, int id, float v) { (void)ctx, (void)id, (void)v; }
static bool start(void *ctx) { return (void)ctx, true; }
static void nop(void *ctx) { (void)ctx; }
static const char *trip(void *ctx) { return (void)ctx, trip_; }

static const rte_param_t params[] = {
    {"sp", "Set speed setpoint [rpm]", "<rpm>", -10000.0f, 10000.0f, 0.0f},
    {"kp", "Set proportional gain", "<value>", 0.0f, 1e6f, 7.2e-4f},
};
static const rte_hooks_t hooks = {set_param, start, nop, nop, trip, NULL};
static rte_t r;

void setUp(void)
{
  trip_ = NULL;
  rte_init(&r, params, 2, &hooks);
  console_init("test> ");
  rte_cmd_register_params(&r);
  rte_cmd_register_run(&r);
}
void tearDown(void) {}

static void test_parameter_commands(void)
{
  TEST_ASSERT_EQUAL(0, console_host_exec("sp 3000"));
  TEST_ASSERT_EQUAL_FLOAT(3000.0f, rte_get(&r, 0));
  TEST_ASSERT_EQUAL(1, console_host_exec("sp 20000")); /* out of range: refused, unchanged */
  TEST_ASSERT_EQUAL(1, console_host_exec("sp"));       /* usage */
  TEST_ASSERT_EQUAL(1, console_host_exec("kp x"));
  TEST_ASSERT_EQUAL_FLOAT(3000.0f, rte_get(&r, 0));
  TEST_ASSERT_EQUAL_STRING("Set speed setpoint [rpm]", console_host_help("sp"));
  TEST_ASSERT_EQUAL(-1, console_host_exec("nope"));
}

static void test_start_stop_clear(void)
{
  TEST_ASSERT_EQUAL(0, console_host_exec("start"));
  TEST_ASSERT_TRUE(rte_running(&r));
  TEST_ASSERT_EQUAL(0, console_host_exec("stop"));
  TEST_ASSERT_FALSE(rte_running(&r));
  trip_ = "overspeed";
  TEST_ASSERT_EQUAL(1, console_host_exec("start")); /* refused while tripped */
  TEST_ASSERT_FALSE(rte_running(&r));
  TEST_ASSERT_EQUAL(0, console_host_exec("clear"));
  TEST_ASSERT_TRUE(r.clear_req);
  TEST_ASSERT_EQUAL_STRING("Clear a latched trip", console_host_help("clear"));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_parameter_commands);
  RUN_TEST(test_start_stop_clear);
  return UNITY_END();
}
