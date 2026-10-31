#include <math.h>
#include <unity.h>

#include "t12_cycle.h"
#include "t12_hw.h"
#include "t12_io_sim.h"

#define AMB 25.0
#define NEAR(expected, actual, tol) near_((expected), (actual), (tol), #actual)

static const t12_params_t P = T12_PARAMS_DEFAULT;

typedef struct {
  t12_io_sim_t sim;
  t12_io_t io;
  t12_cycle_t cyc;
} rig_t;

void setUp(void) {}
void tearDown(void) {}

static void near_(double expected, double actual, double tol, const char *what)
{
  TEST_ASSERT_TRUE_MESSAGE(fabs(expected - actual) <= tol, what);
}

static void rig_init(rig_t *r, uint32_t settle, float duty)
{
  t12_io_sim_init(&r->sim, &r->io, &P, 1e-3, AMB);
  t12_cycle_init(&r->cyc, T12_CYCLE_PERIOD_TICKS, settle, T12_SAMPLE_TICKS);
  t12_cycle_set_duty(&r->cyc, duty);
}

/* One control tick as the firmware runs it: cycle, heartbeat, then the plant advances. */
static bool tick(rig_t *r, t12_meas_t *m)
{
  bool done = t12_cycle_tick(&r->cyc, &r->io, m);
  r->io.heartbeat_toggle(r->io.ctx);
  t12_io_sim_step(&r->sim);
  return done;
}

typedef struct {
  t12_meas_t meas;
  int heater_ticks;
  int meas_tick;
  double true_counts; /* what an ideal reading of the tip would give when the window closed */
} cycle_info_t;

static double settled_counts(const t12_plant_t *p);

static cycle_info_t cycle(rig_t *r)
{
  cycle_info_t info = {.meas_tick = -1};
  for (int k = 0; k < T12_CYCLE_PERIOD_TICKS; k++) {
    if (tick(r, &info.meas)) {
      info.meas_tick = k;
      info.true_counts = settled_counts(&r->sim.plant);
    }
    if (r->sim.heater)
      info.heater_ticks++;
  }
  return info;
}

static double settled_counts(const t12_plant_t *p)
{
  double v = P.amp_gain * (P.seebeck * (p->t_h - p->t_c) + P.amp_offset);
  return v / T12_ADC_VREF * T12_ADC_MAX;
}

static void check_heat(float duty, int expected)
{
  rig_t r;
  rig_init(&r, T12_SETTLE_TICKS, duty);
  cycle_info_t info = cycle(&r);
  TEST_ASSERT_EQUAL_INT(expected, info.heater_ticks);
  TEST_ASSERT_EQUAL_UINT16(expected, info.meas.heat_ticks);
}

static void test_duty_sets_the_heating_ticks(void)
{
  check_heat(0.0f, 0);
  check_heat(0.5f, 47);
  check_heat(1.0f, 93); /* 100 - 3 settle - 4 samples */
  check_heat(2.0f, 93);
  check_heat(-1.0f, 0);
  check_heat(NAN, 0);
}

static void test_one_measurement_per_cycle_behind_the_heating(void)
{
  rig_t r;
  rig_init(&r, T12_SETTLE_TICKS, 0.5f);
  t12_meas_t m;
  int count = 0;
  for (int k = 0; k < 300; k++) {
    if (tick(&r, &m)) {
      TEST_ASSERT_EQUAL_INT(47 + T12_SETTLE_TICKS + T12_SAMPLE_TICKS - 1, k % 100);
      count++;
    }
  }
  TEST_ASSERT_EQUAL_INT(3, count);
}

static void test_duty_applies_from_the_next_cycle(void)
{
  rig_t r;
  rig_init(&r, T12_SETTLE_TICKS, 0.5f);
  t12_meas_t m;
  int heater = 0;
  for (int k = 0; k < 100; k++) {
    if (k == 10)
      t12_cycle_set_duty(&r.cyc, 1.0f);
    tick(&r, &m);
    heater += r.sim.heater;
  }
  TEST_ASSERT_EQUAL_INT(47, heater);
  TEST_ASSERT_EQUAL_INT(93, cycle(&r).heater_ticks);
}

static void test_reading_without_heating_matches_the_tip(void)
{
  rig_t r;
  rig_init(&r, T12_SETTLE_TICKS, 0.0f);
  r.sim.plant.t_h = 125.0;
  r.sim.plant.t_t = 100.0;
  cycle(&r);
  cycle_info_t info = cycle(&r);
  NEAR(info.true_counts, info.meas.tc, 3);
}

static double artifact_counts(uint32_t settle)
{
  rig_t r;
  rig_init(&r, settle, 0.5f);
  cycle(&r);
  cycle_info_t info = cycle(&r);
  return fabs(info.meas.tc - info.true_counts);
}

static void test_a_longer_settle_removes_the_switching_artifact(void)
{
  double settled = artifact_counts(T12_SETTLE_TICKS);
  double rushed = artifact_counts(1);
  TEST_ASSERT_TRUE(settled < 20.0); /* under 3 degC */
  TEST_ASSERT_TRUE(rushed > 5.0 * settled);
}

static void test_heater_current_is_captured_while_heating(void)
{
  rig_t r;
  rig_init(&r, T12_SETTLE_TICKS, 0.5f);
  cycle_info_t info = cycle(&r);
  TEST_ASSERT_TRUE(info.meas.current_valid);
  NEAR(1861, info.meas.current, 1);

  rig_init(&r, T12_SETTLE_TICKS, 0.0f);
  info = cycle(&r);
  TEST_ASSERT_FALSE(info.meas.current_valid);
  TEST_ASSERT_EQUAL_UINT16(0, info.meas.current);

  rig_init(&r, T12_SETTLE_TICKS, 1.0f / 93.0f); /* a single heating tick has no current to see */
  info = cycle(&r);
  TEST_ASSERT_EQUAL_UINT16(1, info.meas.heat_ticks);
  TEST_ASSERT_FALSE(info.meas.current_valid);
}

static void test_handle_ntc_is_passed_through(void)
{
  rig_t r;
  rig_init(&r, T12_SETTLE_TICKS, 0.0f);
  cycle_info_t info = cycle(&r);
  NEAR(2047.5, info.meas.ntc, 1);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_duty_sets_the_heating_ticks);
  RUN_TEST(test_one_measurement_per_cycle_behind_the_heating);
  RUN_TEST(test_duty_applies_from_the_next_cycle);
  RUN_TEST(test_reading_without_heating_matches_the_tip);
  RUN_TEST(test_a_longer_settle_removes_the_switching_artifact);
  RUN_TEST(test_heater_current_is_captured_while_heating);
  RUN_TEST(test_handle_ntc_is_passed_through);
  return UNITY_END();
}
