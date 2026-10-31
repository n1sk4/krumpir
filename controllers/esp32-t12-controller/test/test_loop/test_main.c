#include <math.h>
#include <unity.h>

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
  TEST_ASSERT_EQUAL_UINT32(10, r.loop.cycles);
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

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_one_measurement_per_cycle);
  RUN_TEST(test_stopped_loop_does_not_heat_but_keeps_watching);
  RUN_TEST(test_started_loop_heats_to_the_setpoint);
  RUN_TEST(test_a_trip_stops_the_loop_and_blocks_start_until_cleared);
  RUN_TEST(test_stop_returns_to_zero_duty);
  return UNITY_END();
}
