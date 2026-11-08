#include <math.h>
#include <unity.h>

#include "t12_cycle.h"
#include "t12_hw.h"
#include "t12_io_sim.h"
#include "t12_loop.h"

static const t12_params_t P = T12_PARAMS_DEFAULT;
static const t12_ctrl_cfg_t CTRL = T12_CTRL_CFG_DEFAULT;
static const t12_safety_cfg_t SAFE = T12_SAFETY_CFG_DEFAULT;
static const t12_cal_t CAL = T12_CAL_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

typedef struct {
  t12_io_sim_t sim;
  t12_io_t io;
  t12_loop_t loop;
} rig_t;

static void rig_init(rig_t *r)
{
  t12_io_sim_init(&r->sim, &r->io, &P, 1e-3, 25.0);
  t12_loop_init(&r->loop, &CTRL, &SAFE, &CAL, 1e-3f);
  t12_ctrl_set_setpoint(&r->loop.ctrl, 350.0f);
}

static int run(rig_t *r, double seconds)
{
  int fresh = 0;
  for (long k = 0; k < (long)(seconds * 1000); k++) {
    fresh += t12_loop_tick(&r->loop, &r->io);
    t12_io_sim_step(&r->sim);
  }
  return fresh;
}

static void test_one_measurement_per_cycle(void)
{
  rig_t r;
  rig_init(&r);
  TEST_ASSERT_EQUAL_INT(10, run(&r, 1.0));
  TEST_ASSERT_TRUE(fabsf(r.loop.handle - 25.0f) < 0.5f);
}

static void test_stopped_loop_does_not_heat_but_keeps_watching(void)
{
  rig_t r;
  rig_init(&r);
  run(&r, 3.0);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.loop.duty);
  TEST_ASSERT_TRUE(fabs(r.sim.plant.t_t - 25.0) < 1e-6);
  TEST_ASSERT_TRUE(t12_safety_ok(&r.loop.safety));
}

static void test_started_loop_heats_to_the_setpoint(void)
{
  rig_t r;
  rig_init(&r);
  run(&r, 1.0);
  TEST_ASSERT_TRUE(t12_loop_start(&r.loop));
  run(&r, 30.0);
  TEST_ASSERT_TRUE(fabsf(r.loop.temp - 350.0f) < 1.0f);
}

static void test_a_trip_stops_the_loop_and_blocks_start_until_cleared(void)
{
  rig_t r;
  rig_init(&r);
  r.sim.plant.fault = T12_FAULT_STUCK_ON;
  run(&r, 0.5);
  TEST_ASSERT_EQUAL(T12_TRIP_STUCK_ON, r.loop.safety.latched);
  TEST_ASSERT_FALSE(t12_loop_start(&r.loop));
  run(&r, 0.3);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)r.sim.plant.i_heater); /* heartbeat gone, Q3 open */

  r.sim.plant.fault = T12_FAULT_NONE;
  t12_loop_clear(&r.loop);
  r.io.heartbeat_toggle(r.io.ctx); /* the loop does this on its next tick */
  TEST_ASSERT_TRUE(t12_loop_start(&r.loop));
  run(&r, 20.0);
  TEST_ASSERT_TRUE(t12_safety_ok(&r.loop.safety));
  TEST_ASSERT_TRUE(r.loop.temp > 300.0f);
}

static void test_stop_returns_to_zero_duty(void)
{
  rig_t r;
  rig_init(&r);
  t12_loop_start(&r.loop);
  run(&r, 5.0);
  TEST_ASSERT_TRUE(r.loop.duty > 0.5f);
  t12_loop_stop(&r.loop);
  run(&r, 1.0);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.loop.duty);
}

/* The simulated board behind a wrapper: from tick `lost_from` on every reading is T12_ADC_NONE
 * (what src/io_hil.c reads once the HIL link is gone); on tick `open_at` the thermocouple reads
 * full scale once (an open input for one sample). */
static struct {
  t12_io_t real;
  long tick, lost_from, open_at;
} w;

static bool gone(void) { return w.lost_from >= 0 && w.tick >= w.lost_from; }
static void w_heater(void *c, bool on) { (void)c, w.real.heater_set(w.real.ctx, on); }
static uint16_t w_tc(void *c)
{
  (void)c;
  return gone() ? T12_ADC_NONE : w.tick == w.open_at ? T12_ADC_MAX : w.real.tc_read(w.real.ctx);
}
static uint16_t w_ntc(void *c)
{
  (void)c;
  return gone() ? T12_ADC_NONE : w.real.ntc_read(w.real.ctx);
}
static uint16_t w_cur(void *c)
{
  (void)c;
  return gone() ? T12_ADC_NONE : w.real.current_read(w.real.ctx);
}
static void w_hb(void *c) { (void)c, w.real.heartbeat_toggle(w.real.ctx); }
static void w_safe(void *c) { (void)c, w.real.safe_state(w.real.ctx); }

/* Heats to the setpoint, then runs until `until` behind the wrapper; returns the trip tick. */
static long run_wrapped(rig_t *r, long lost_from, long open_at, long until)
{
  rig_init(r);
  w.real = r->io;
  w.tick = 0, w.lost_from = lost_from, w.open_at = open_at;
  const t12_io_t io = {w_heater, w_tc, w_ntc, w_cur, w_hb, w_safe, NULL};
  t12_loop_start(&r->loop);
  long trip = -1;
  for (; w.tick < until; w.tick++) {
    t12_loop_tick(&r->loop, &io);
    if (trip < 0 && !t12_safety_ok(&r->loop.safety))
      trip = w.tick;
    t12_io_sim_step(&r->sim);
  }
  return trip;
}

/* A lost link reads as a sensor fault at every phase of the heater cycle, never as a tip or heater
 * fault from readings held over: 5 invalid cycles, so within 0.6 s. */
static void test_a_lost_link_trips_sensor_timeout_at_any_cycle_phase(void)
{
  rig_t r;
  for (long phase = 0; phase < T12_CYCLE_PERIOD_TICKS; phase++) {
    const long loss = 8000 + phase;
    const long trip = run_wrapped(&r, loss, -1, loss + 1000);
    TEST_ASSERT_EQUAL_STRING("sensor timeout", t12_trip_name(r.loop.safety.latched));
    TEST_ASSERT_TRUE(trip > loss && trip - loss <= 600);
  }
}

/* One thermocouple sample at full scale makes that cycle's temperature invalid, not a bogus hot
 * average (which tripped over temperature); a single invalid reading is ignored. */
static void test_one_open_sample_is_an_invalid_reading_not_a_hot_one(void)
{
  rig_t r;
  for (long at = 8000; at < 8000 + T12_CYCLE_PERIOD_TICKS; at++) {
    TEST_ASSERT_EQUAL(-1, run_wrapped(&r, -1, at, 8300));
    TEST_ASSERT_TRUE(fabsf(r.loop.temp - 350.0f) < 2.0f);
  }
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_one_measurement_per_cycle);
  RUN_TEST(test_stopped_loop_does_not_heat_but_keeps_watching);
  RUN_TEST(test_started_loop_heats_to_the_setpoint);
  RUN_TEST(test_a_trip_stops_the_loop_and_blocks_start_until_cleared);
  RUN_TEST(test_stop_returns_to_zero_duty);
  RUN_TEST(test_a_lost_link_trips_sensor_timeout_at_any_cycle_phase);
  RUN_TEST(test_one_open_sample_is_an_invalid_reading_not_a_hot_one);
  return UNITY_END();
}
