#include <math.h>
#include <string.h>
#include <unity.h>

#include "t12_convert.h"
#include "t12_ctrl.h"
#include "t12_cycle.h"
#include "t12_hw.h"
#include "t12_io_sim.h"
#include "t12_safety.h"

static const t12_params_t P = T12_PARAMS_DEFAULT;
static const t12_cal_t CAL = T12_CAL_DEFAULT;
static const t12_ctrl_cfg_t CTRL = T12_CTRL_CFG_DEFAULT;
static const t12_safety_cfg_t SAFE = T12_SAFETY_CFG_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

/* ---- the supervisor alone, fed synthetic measurements ---- */

static t12_meas_t healthy(float duty)
{
  t12_meas_t m = {.tc = 1000,
                  .ntc = 2048,
                  .current = 1861, /* 3 A */
                  .current_off = 0,
                  .current_valid = true,
                  .heat_ticks = (uint16_t)(duty * 93),
                  .duty = duty};
  return m;
}

static t12_trip_t feed(t12_safety_t *s, t12_meas_t m, float temp)
{
  return t12_safety_update(s, &m, temp);
}

static void test_healthy_cycles_do_not_trip(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  for (int k = 0; k < 50; k++)
    TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.5f), 350.0f));
  TEST_ASSERT_TRUE(t12_safety_ok(&s));
}

static void test_over_temperature(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.1f), 479.0f));
  TEST_ASSERT_EQUAL(T12_TRIP_OVER_TEMP, feed(&s, healthy(0.1f), 481.0f));
  TEST_ASSERT_FALSE(t12_safety_ok(&s));
}

static void test_sensor_needs_consecutive_invalid_readings(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  for (int k = 0; k < 4; k++)
    TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.5f), NAN));
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.5f), 300.0f)); /* a good one resets */
  for (int k = 0; k < 4; k++)
    TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.5f), NAN));
  TEST_ASSERT_EQUAL(T12_TRIP_SENSOR, feed(&s, healthy(0.5f), NAN));
}

static void test_heater_current_checks(void)
{
  t12_safety_t s;
  t12_meas_t m;

  t12_safety_init(&s, &SAFE);
  m = healthy(0.5f);
  m.current = 0;
  TEST_ASSERT_EQUAL(T12_TRIP_NO_TIP, feed(&s, m, 300.0f));

  t12_safety_init(&s, &SAFE);
  m.current_valid = false; /* a one tick cycle has no current to judge */
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, m, 300.0f));

  t12_safety_init(&s, &SAFE);
  m = healthy(0.5f);
  m.current = 2540; /* 5.9 ohm */
  TEST_ASSERT_EQUAL(T12_TRIP_SHORT, feed(&s, m, 300.0f));

  t12_safety_init(&s, &SAFE);
  m.current = 2420; /* 6.1 ohm */
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, m, 300.0f));

  t12_safety_init(&s, &SAFE);
  m.current = 1450; /* 10.3 ohm */
  TEST_ASSERT_EQUAL(T12_TRIP_RESISTANCE, feed(&s, m, 300.0f));

  t12_safety_init(&s, &SAFE);
  m.current = 1500; /* 9.9 ohm */
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, m, 300.0f));
}

static void test_current_while_commanded_off(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  t12_meas_t m = healthy(0.0f);
  m.current_valid = false;
  m.current_off = 20; /* offset and noise of an idle sense amplifier */
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, m, 300.0f));
  m.current_off = 1861;
  TEST_ASSERT_EQUAL(T12_TRIP_STUCK_ON, feed(&s, m, 300.0f));
}

static void test_full_power_without_a_rise_is_runaway(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  for (int k = 1; k < 200; k++)
    TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.93f), 100.0f));
  TEST_ASSERT_EQUAL(T12_TRIP_RUNAWAY, feed(&s, healthy(0.93f), 100.0f)); /* the 200th, 20 s */
}

static void test_a_rise_or_lower_duty_restarts_the_runaway_window(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  for (int k = 0; k < 400; k++) /* +0.5 K per cycle: 50 K every 100 cycles */
    TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.93f), 100.0f + 0.5f * k));

  t12_safety_init(&s, &SAFE);
  for (int k = 0; k < 150; k++)
    feed(&s, healthy(0.93f), 100.0f);
  feed(&s, healthy(0.5f), 100.0f); /* the controller backed off: the window restarts */
  for (int k = 1; k < 200; k++)
    TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.93f), 100.0f));
}

static void test_trip_latches_until_cleared(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  feed(&s, healthy(0.1f), 500.0f);
  TEST_ASSERT_EQUAL(T12_TRIP_OVER_TEMP, feed(&s, healthy(0.1f), 300.0f)); /* still latched */
  t12_safety_clear(&s);
  TEST_ASSERT_TRUE(t12_safety_ok(&s));
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, feed(&s, healthy(0.1f), 300.0f));
}

static void test_the_first_trip_in_priority_order_wins(void)
{
  t12_safety_t s;
  t12_safety_init(&s, &SAFE);
  t12_meas_t m = healthy(0.5f);
  m.current_off = 1861;
  TEST_ASSERT_EQUAL(T12_TRIP_OVER_TEMP, feed(&s, m, 500.0f));
}

static void test_every_trip_has_a_name(void)
{
  for (int t = T12_TRIP_NONE; t <= T12_TRIP_RUNAWAY; t++)
    TEST_ASSERT_TRUE(strcmp(t12_trip_name((t12_trip_t)t), "unknown") != 0);
}

/* ---- the supervisor in the control loop, against injected faults on the simulated tip ---- */

typedef struct {
  t12_io_sim_t sim;
  t12_io_t io;
  t12_cycle_t cyc;
  t12_ctrl_t ctrl;
  t12_safety_t safety;
  float temp;
} rig_t;

static void rig_init(rig_t *r, const t12_params_t *p)
{
  t12_io_sim_init(&r->sim, &r->io, p, 1e-3, 25.0);
  t12_cycle_init(&r->cyc, T12_CYCLE_PERIOD_TICKS, T12_SETTLE_TICKS, T12_SAMPLE_TICKS);
  t12_ctrl_init(&r->ctrl, &CTRL, 0.1f);
  t12_ctrl_set_setpoint(&r->ctrl, 350.0f);
  t12_ctrl_start(&r->ctrl, 25.0f);
  t12_safety_init(&r->safety, &SAFE);
  r->temp = 25.0f;
}

static void run(rig_t *r, double seconds)
{
  for (long k = 0; k < (long)(seconds * 1000); k++) {
    t12_meas_t m;
    if (t12_cycle_tick(&r->cyc, &r->io, &m)) {
      r->temp = t12_tip_celsius(&CAL, m.tc, m.ntc);
      if (t12_safety_update(&r->safety, &m, r->temp) != T12_TRIP_NONE) {
        t12_ctrl_stop(&r->ctrl);
        t12_cycle_set_duty(&r->cyc, 0.0f);
      } else {
        t12_cycle_set_duty(&r->cyc, t12_ctrl_update(&r->ctrl, r->temp));
      }
    }
    if (t12_safety_ok(&r->safety))
      r->io.heartbeat_toggle(r->io.ctx); /* only a healthy loop keeps the series switch on */
    else
      r->io.safe_state(r->io.ctx);
    t12_io_sim_step(&r->sim);
  }
}

/* The trip must come quickly and leave the heater dead, whatever the switches were doing. */
static void check_trips(rig_t *r, t12_trip_t expected, double within_s)
{
  run(r, within_s);
  TEST_ASSERT_EQUAL(expected, r->safety.latched);
  run(r, 0.3);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)r->sim.plant.i_heater);
}

static void test_healthy_run_never_trips(void)
{
  rig_t r;
  rig_init(&r, &P);
  run(&r, 40.0);
  t12_params_t touching = P;
  touching.g_load = 0.1; /* a big joint */
  t12_plant_set_params(&r.sim.plant, &touching, 1e-3);
  run(&r, 40.0);
  TEST_ASSERT_EQUAL(T12_TRIP_NONE, r.safety.latched);
  TEST_ASSERT_TRUE(r.temp > 340.0f);
}

static void test_missing_tip(void)
{
  rig_t r;
  rig_init(&r, &P);
  r.sim.plant.fault = T12_FAULT_OPEN;
  check_trips(&r, T12_TRIP_NO_TIP, 0.5);
}

static void test_shorted_tip(void)
{
  rig_t r;
  rig_init(&r, &P);
  r.sim.plant.fault = T12_FAULT_SHORT;
  r.sim.plant.v_amp = 0.0; /* keep the first reading valid */
  check_trips(&r, T12_TRIP_SHORT, 0.5);
}

static void test_wrong_heater_resistance(void)
{
  rig_t r;
  t12_params_t weak = P;
  weak.r_coil = 14.0;
  rig_init(&r, &weak);
  check_trips(&r, T12_TRIP_RESISTANCE, 0.5);
}

static void test_stuck_switch_is_cut_by_the_heartbeat(void)
{
  rig_t r;
  rig_init(&r, &P);
  r.sim.plant.fault = T12_FAULT_STUCK_ON;
  check_trips(&r, T12_TRIP_STUCK_ON, 0.5);
  TEST_ASSERT_FALSE(r.sim.plant.series_on);
}

static void test_a_tip_that_cannot_heat_trips_runaway(void)
{
  rig_t r;
  t12_params_t drained = P;
  drained.g_load = 0.5; /* more heat lost than the 72 W supply */
  rig_init(&r, &drained);
  check_trips(&r, T12_TRIP_RUNAWAY, 60.0);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_healthy_cycles_do_not_trip);
  RUN_TEST(test_over_temperature);
  RUN_TEST(test_sensor_needs_consecutive_invalid_readings);
  RUN_TEST(test_heater_current_checks);
  RUN_TEST(test_current_while_commanded_off);
  RUN_TEST(test_full_power_without_a_rise_is_runaway);
  RUN_TEST(test_a_rise_or_lower_duty_restarts_the_runaway_window);
  RUN_TEST(test_trip_latches_until_cleared);
  RUN_TEST(test_the_first_trip_in_priority_order_wins);
  RUN_TEST(test_every_trip_has_a_name);
  RUN_TEST(test_healthy_run_never_trips);
  RUN_TEST(test_missing_tip);
  RUN_TEST(test_shorted_tip);
  RUN_TEST(test_wrong_heater_resistance);
  RUN_TEST(test_stuck_switch_is_cut_by_the_heartbeat);
  RUN_TEST(test_a_tip_that_cannot_heat_trips_runaway);
  return UNITY_END();
}
