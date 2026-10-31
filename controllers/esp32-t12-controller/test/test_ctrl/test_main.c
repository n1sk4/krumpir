#include <math.h>
#include <unity.h>

#include "t12_convert.h"
#include "t12_ctrl.h"
#include "t12_cycle.h"
#include "t12_io_sim.h"

#define NEAR(expected, actual, tol) near_((expected), (actual), (tol), #actual)

static const t12_params_t P = T12_PARAMS_DEFAULT;
static const t12_cal_t CAL = T12_CAL_DEFAULT;
static const t12_ctrl_cfg_t CFG = T12_CTRL_CFG_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

static void near_(double expected, double actual, double tol, const char *what)
{
  TEST_ASSERT_TRUE_MESSAGE(fabs(expected - actual) <= tol, what);
}

static void test_setpoint_is_clamped(void)
{
  t12_ctrl_t c;
  t12_ctrl_init(&c, &CFG, 0.1f);
  t12_ctrl_set_setpoint(&c, 900.0f);
  TEST_ASSERT_EQUAL_FLOAT(450.0f, c.setpoint);
  t12_ctrl_set_setpoint(&c, 20.0f);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, c.setpoint);
  t12_ctrl_set_setpoint(&c, 320.0f);
  TEST_ASSERT_EQUAL_FLOAT(320.0f, c.setpoint);
}

static void test_stopped_gives_no_duty(void)
{
  t12_ctrl_t c;
  t12_ctrl_init(&c, &CFG, 0.1f);
  t12_ctrl_set_setpoint(&c, 350.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, t12_ctrl_update(&c, 25.0f));
  t12_ctrl_start(&c, 25.0f);
  TEST_ASSERT_TRUE(t12_ctrl_update(&c, 25.0f) > 0.9f); /* 325 K of error saturates the duty */
  t12_ctrl_stop(&c);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, t12_ctrl_update(&c, 25.0f));
}

static void test_hot_tip_gets_no_duty(void)
{
  t12_ctrl_t c;
  t12_ctrl_init(&c, &CFG, 0.1f);
  t12_ctrl_set_setpoint(&c, 300.0f);
  t12_ctrl_start(&c, 400.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, t12_ctrl_update(&c, 400.0f));
}

static void test_ramp_limits_the_setpoint_rate(void)
{
  t12_ctrl_cfg_t cfg = CFG;
  cfg.ramp = 10.0f; /* 1 K per 0.1 s update */
  t12_ctrl_t c;
  t12_ctrl_init(&c, &cfg, 0.1f);
  t12_ctrl_set_setpoint(&c, 350.0f);
  t12_ctrl_start(&c, 25.0f);
  for (int k = 1; k <= 10; k++) {
    t12_ctrl_update(&c, 25.0f);
    NEAR(25.0 + k, c.sp_active, 1e-3);
  }
  for (int k = 0; k < 400; k++)
    t12_ctrl_update(&c, 25.0f);
  TEST_ASSERT_EQUAL_FLOAT(350.0f, c.sp_active); /* arrives exactly, never beyond */

  t12_ctrl_set_setpoint(&c, 340.0f); /* ramps down as well */
  t12_ctrl_update(&c, 350.0f);
  NEAR(349.0, c.sp_active, 1e-3);
}

static void test_non_finite_reading_holds_the_duty(void)
{
  t12_ctrl_t c;
  t12_ctrl_init(&c, &CFG, 0.1f);
  t12_ctrl_set_setpoint(&c, 350.0f);
  t12_ctrl_start(&c, 340.0f);
  float duty = t12_ctrl_update(&c, 340.0f);
  TEST_ASSERT_EQUAL_FLOAT(duty, t12_ctrl_update(&c, NAN));
}

/* The control loop as the firmware runs it, on the simulated tip. */
typedef struct {
  t12_io_sim_t sim;
  t12_io_t io;
  t12_cycle_t cyc;
  t12_ctrl_t ctrl;
  float temp;     /* last measurement [degC] */
  float duty_max; /* largest duty handed out */
  float duty_min;
} rig_t;

static void rig_init(rig_t *r, float setpoint)
{
  t12_io_sim_init(&r->sim, &r->io, &P, 1e-3, 25.0);
  t12_cycle_init(&r->cyc, T12_CYCLE_PERIOD_TICKS, T12_SETTLE_TICKS, T12_SAMPLE_TICKS);
  t12_ctrl_init(&r->ctrl, &CFG, 0.1f);
  t12_ctrl_set_setpoint(&r->ctrl, setpoint);
  t12_ctrl_start(&r->ctrl, 25.0f);
  r->temp = 25.0f;
  r->duty_max = 0.0f;
  r->duty_min = 1.0f;
}

/* Runs for the given seconds; returns the peak measurement and the lowest one after min_from. */
static void run(rig_t *r, double seconds, double min_from, float *peak, float *low)
{
  *peak = -1e9f;
  *low = 1e9f;
  for (long k = 0; k < (long)(seconds * 1000); k++) {
    t12_meas_t m;
    if (t12_cycle_tick(&r->cyc, &r->io, &m)) {
      r->temp = t12_tip_celsius(&CAL, m.tc, m.ntc);
      float duty = t12_ctrl_update(&r->ctrl, r->temp);
      t12_cycle_set_duty(&r->cyc, duty);
      r->duty_max = duty > r->duty_max ? duty : r->duty_max;
      r->duty_min = duty < r->duty_min ? duty : r->duty_min;
      if (r->temp > *peak)
        *peak = r->temp;
      if (k * 1e-3 >= min_from && r->temp < *low)
        *low = r->temp;
    }
    r->io.heartbeat_toggle(r->io.ctx);
    t12_io_sim_step(&r->sim);
  }
}

static void test_cold_start_reaches_the_setpoint_without_overshoot(void)
{
  rig_t r;
  rig_init(&r, 350.0f);
  float peak, low;
  run(&r, 8.0, 0.0, &peak, &low);
  TEST_ASSERT_TRUE(r.temp > 340.0f); /* about 5 s from cold */
  run(&r, 30.0, 0.0, &peak, &low);
  TEST_ASSERT_TRUE(peak < 355.0f);
  NEAR(350.0, r.temp, 1.0);
  TEST_ASSERT_TRUE(r.duty_min >= 0.0f && r.duty_max <= 1.0f);
}

static void test_joint_load_is_rejected(void)
{
  rig_t r;
  rig_init(&r, 350.0f);
  float peak, low;
  run(&r, 30.0, 0.0, &peak, &low);
  t12_params_t touching = P;
  touching.g_load = 0.1; /* about 30 W drawn by a big joint */
  t12_plant_set_params(&r.sim.plant, &touching, 1e-3);
  run(&r, 30.0, 0.0, &peak, &low);
  TEST_ASSERT_TRUE(low > 330.0f);
  NEAR(350.0, r.temp, 1.5);
}

static void test_integrator_does_not_wind_up_with_the_heater_unable_to_heat(void)
{
  rig_t r;
  rig_init(&r, 350.0f);
  r.sim.plant.fault = T12_FAULT_OPEN; /* no tip: the loop saturates for a long time */
  float peak, low;
  run(&r, 30.0, 0.0, &peak, &low);
  r.sim.plant.fault = T12_FAULT_NONE;
  run(&r, 20.0, 0.0, &peak, &low);
  TEST_ASSERT_TRUE(peak < 360.0f);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_setpoint_is_clamped);
  RUN_TEST(test_stopped_gives_no_duty);
  RUN_TEST(test_hot_tip_gets_no_duty);
  RUN_TEST(test_ramp_limits_the_setpoint_rate);
  RUN_TEST(test_non_finite_reading_holds_the_duty);
  RUN_TEST(test_cold_start_reaches_the_setpoint_without_overshoot);
  RUN_TEST(test_joint_load_is_rejected);
  RUN_TEST(test_integrator_does_not_wind_up_with_the_heater_unable_to_heat);
  return UNITY_END();
}
