#include <string.h>
#include <unity.h>

#include "rte.h"

/* A fake component: records the hooks in call order. */
static char log_[256];
static const char *trip_;
static bool refuse_start;
static float values[3];

static void note(const char *s) { strcat(log_, s); }
static void set_param(void *ctx, int id, float v)
{
  (void)ctx;
  values[id] = v;
  note(id == 0 ? "p0," : id == 1 ? "p1," : "p2,");
}
static bool start(void *ctx) { return (void)ctx, note("start,"), !refuse_start; }
static void stop(void *ctx) { (void)ctx, note("stop,"); }
static void clear(void *ctx) { (void)ctx, note("clear,"), trip_ = NULL; }
static const char *trip(void *ctx) { return (void)ctx, trip_; }

static const rte_param_t params[] = {
    {"sp", "Set the setpoint", "<x>", -10.0f, 10.0f, 1.0f},
    {"kp", "Set kp", "<value>", 0.0f, 5.0f, 2.0f},
    {"ki", "Set ki", "<value>", 0.0f, 5.0f, 3.0f},
};
static const rte_hooks_t hooks = {set_param, start, stop, clear, trip, NULL};
static rte_t r;

void setUp(void)
{
  log_[0] = '\0';
  trip_ = NULL;
  refuse_start = false;
  rte_init(&r, params, 3, &hooks);
}
void tearDown(void) {}

static void test_first_tick_applies_every_default(void)
{
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("p0,p1,p2,", log_);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, values[1]);
  log_[0] = '\0';
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("", log_); /* nothing changed */
}

static void test_set_checks_the_range_and_applies_on_the_tick(void)
{
  rte_tick_begin(&r);
  log_[0] = '\0';
  TEST_ASSERT_TRUE(rte_set(&r, 1, 4.5f));
  TEST_ASSERT_FALSE(rte_set(&r, 1, 6.0f)); /* above max */
  TEST_ASSERT_FALSE(rte_set(&r, 7, 1.0f)); /* unknown */
  TEST_ASSERT_EQUAL_FLOAT(4.5f, rte_get(&r, 1));
  TEST_ASSERT_EQUAL_STRING("", log_); /* not before the tick */
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("p1,", log_);
  TEST_ASSERT_EQUAL_FLOAT(4.5f, values[1]);
  TEST_ASSERT_EQUAL(1, rte_find(&r, "kp"));
  TEST_ASSERT_EQUAL(-1, rte_find(&r, "nope"));
}

static void test_order_clear_params_run(void)
{
  rte_tick_begin(&r);
  log_[0] = '\0';
  rte_set(&r, 0, 5.0f);
  rte_set_run(&r, true);
  rte_request_clear(&r);
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("clear,p0,start,", log_);
  log_[0] = '\0';
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("", log_); /* clear and start only once */
  rte_set_run(&r, false);
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("stop,", log_);
}

static void test_trip_drops_the_run_request(void)
{
  rte_set_run(&r, true);
  rte_tick_begin(&r);
  rte_tick_end(&r);
  TEST_ASSERT_TRUE(rte_running(&r));
  trip_ = "overspeed"; /* the component trips during its tick */
  rte_tick_end(&r);
  TEST_ASSERT_FALSE(rte_running(&r));
  TEST_ASSERT_EQUAL_STRING("overspeed", rte_trip(&r));
  log_[0] = '\0';
  rte_tick_begin(&r);
  TEST_ASSERT_EQUAL_STRING("stop,", log_); /* the run change reaches the component */
}

static void test_refused_start_drops_the_run_request(void)
{
  refuse_start = true;
  rte_set_run(&r, true);
  rte_tick_begin(&r);
  rte_tick_end(&r);
  TEST_ASSERT_FALSE(rte_running(&r));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_first_tick_applies_every_default);
  RUN_TEST(test_set_checks_the_range_and_applies_on_the_tick);
  RUN_TEST(test_order_clear_params_run);
  RUN_TEST(test_trip_drops_the_run_request);
  RUN_TEST(test_refused_start_drops_the_run_request);
  return UNITY_END();
}
