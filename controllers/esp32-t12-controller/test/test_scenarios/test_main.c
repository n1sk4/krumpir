#include <math.h>
#include <string.h>
#include <unity.h>

#include "t12_scenario.h"

static const t12_ctrl_cfg_t CFG = T12_CTRL_CFG_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

typedef struct {
  int rows;
  float last_t;
} count_t;

static void count_rows(void *ctx, const t12_scn_sample_t *s)
{
  count_t *c = ctx;
  c->rows++;
  c->last_t = s->t_s;
}

static t12_scn_result_t run(t12_scenario_t id) { return t12_scenario_run(id, &CFG, NULL, NULL); }

static void test_cold_start(void)
{
  t12_scn_result_t r = run(T12_SCN_COLD_START);
  TEST_ASSERT_TRUE(r.overshoot_pct < 2.0f);
  TEST_ASSERT_TRUE(r.settling_s < 8.0f);
  TEST_ASSERT_TRUE(fabsf(r.final_error) < 1.0f);
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, r.trip);
}

static void test_joint_load_step(void)
{
  t12_scn_result_t r = run(T12_SCN_LOAD_STEP);
  TEST_ASSERT_TRUE(r.undershoot_pct < 5.0f);
  TEST_ASSERT_TRUE(r.settling_s < 2.0f);
  TEST_ASSERT_TRUE(fabsf(r.final_error) < 1.5f);
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, r.trip);
}

static void test_ambient_step(void)
{
  t12_scn_result_t r = run(T12_SCN_AMBIENT_STEP);
  TEST_ASSERT_TRUE(r.undershoot_pct < 2.0f);
  TEST_ASSERT_TRUE(fabsf(r.final_error) < 1.5f);
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, r.trip);
}

static void test_open_tip_trips_at_once(void)
{
  t12_scn_result_t r = run(T12_SCN_OPEN_TIP);
  TEST_ASSERT_EQUAL(T12_TRIP_NO_TIP, r.trip);
  TEST_ASSERT_TRUE(r.trip_s >= 0.0f && r.trip_s < 0.5f);
}

static void test_shorted_tip_trips_at_once(void)
{
  t12_scn_result_t r = run(T12_SCN_SHORTED_TIP);
  TEST_ASSERT_EQUAL(T12_TRIP_SHORT, r.trip);
  TEST_ASSERT_TRUE(r.trip_s >= 0.0f && r.trip_s < 0.5f);
}

static void test_stuck_switch_trips_at_once(void)
{
  t12_scn_result_t r = run(T12_SCN_STUCK_SWITCH);
  TEST_ASSERT_EQUAL(T12_TRIP_STUCK_ON, r.trip);
  TEST_ASSERT_TRUE(r.trip_s >= 0.0f && r.trip_s < 0.5f);
}

static void test_runaway_trips_after_about_twenty_seconds(void)
{
  t12_scn_result_t r = run(T12_SCN_RUNAWAY);
  TEST_ASSERT_EQUAL(T12_TRIP_RUNAWAY, r.trip);
  TEST_ASSERT_TRUE(r.trip_s > 19.0f && r.trip_s < 25.0f);
}

static void test_sink_gets_a_row_per_measurement(void)
{
  count_t c = {0};
  t12_scenario_run(T12_SCN_COLD_START, &CFG, count_rows, &c);
  TEST_ASSERT_TRUE(c.rows >= 299 && c.rows <= 300); /* 30 s at 10 Hz */
  TEST_ASSERT_TRUE(c.last_t > 29.8f);
}

static void test_scenarios_have_distinct_names(void)
{
  for (int a = 0; a < T12_SCN_COUNT; a++)
    for (int b = a + 1; b < T12_SCN_COUNT; b++)
      TEST_ASSERT_TRUE(strcmp(t12_scenario_name((t12_scenario_t)a),
                              t12_scenario_name((t12_scenario_t)b)) != 0);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_cold_start);
  RUN_TEST(test_joint_load_step);
  RUN_TEST(test_ambient_step);
  RUN_TEST(test_open_tip_trips_at_once);
  RUN_TEST(test_shorted_tip_trips_at_once);
  RUN_TEST(test_stuck_switch_trips_at_once);
  RUN_TEST(test_runaway_trips_after_about_twenty_seconds);
  RUN_TEST(test_sink_gets_a_row_per_measurement);
  RUN_TEST(test_scenarios_have_distinct_names);
  return UNITY_END();
}
