#include <math.h>
#include <unity.h>

#include "t12_plant.h"

#define AMB 25.0
#define NEAR(expected, actual, tol) \
  TEST_ASSERT_TRUE_MESSAGE(fabs((expected) - (actual)) <= (tol), #actual " out of tolerance")

static const t12_params_t P = T12_PARAMS_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

static void run(t12_plant_t *s, int steps, bool on)
{
  for (int k = 0; k < steps; k++)
    t12_plant_step(s, on, AMB);
}

/* Steady state for a constant heater power: heat flows heater -> tip -> (air + handle -> air). */
static void steady(const t12_params_t *p, double power, double *t_h, double *t_t, double *t_c)
{
  double g = 1.0 / p->r_ta + p->g_load;
  double q = power / (1.0 + (p->r_ca + p->r_tc) * g); /* heat flowing into the handle */
  *t_c = AMB + q * p->r_ca;
  *t_t = AMB + q * (p->r_ca + p->r_tc);
  *t_h = *t_t + power * p->r_ht;
}

static void check_steady(double g_load)
{
  t12_params_t p = P;
  p.v_supply = 5.0; /* 3.1 W: the temperatures stay in a believable range */
  p.g_load = g_load;
  t12_plant_t s;
  t12_plant_init(&s, &p, 0.1, AMB);
  run(&s, 40000, true); /* 4000 s, about 18 slowest time constants */
  double h, t, c;
  steady(&p, 5.0 * 5.0 / 8.0, &h, &t, &c);
  NEAR(h, s.t_h, 0.01);
  NEAR(t, s.t_t, 0.01);
  NEAR(c, s.t_c, 0.01);
}

static void test_steady_state(void) { check_steady(0.0); }
static void test_steady_state_with_load(void) { check_steady(0.1); }

static void test_energy_balance(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  run(&s, 500, true);
  double stored = P.c_h * (s.t_h - AMB) + P.c_t * (s.t_t - AMB) + P.c_c * (s.t_c - AMB);
  TEST_ASSERT_TRUE(stored < 36.0);        /* 72 W for 0.5 s; nothing is created */
  TEST_ASSERT_TRUE(stored > 0.97 * 36.0); /* only a little has leaked to the air */
}

static void test_cooling_matches_single_exponential(void)
{
  t12_params_t p = P;
  p.r_ht = 0.01; /* heater and tip move together */
  p.r_tc = 1e9;  /* handle isolated */
  p.r_ca = 1e9;
  t12_plant_t s;
  t12_plant_init(&s, &p, 1e-3, AMB);
  s.t_h = s.t_t = 350.0;
  run(&s, 10000, false);
  double tau = (p.c_h + p.c_t) * p.r_ta;
  NEAR(AMB + 325.0 * exp(-10.0 / tau), s.t_t, 0.3);
}

static void test_independent_of_step_size(void)
{
  t12_plant_t coarse, fine;
  t12_plant_init(&coarse, &P, 1e-3, AMB);
  t12_plant_init(&fine, &P, 1e-4, AMB);
  run(&coarse, 300, true);
  run(&fine, 3000, true);
  NEAR(fine.t_h, coarse.t_h, 1e-6);
  NEAR(fine.t_t, coarse.t_t, 1e-6);
  NEAR(fine.t_c, coarse.t_c, 1e-6);
}

static void test_default_tip_heats_in_a_few_seconds(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-2, AMB);
  int k = 0;
  while (s.t_t < 350.0 && k < 5000) {
    t12_plant_step(&s, true, AMB);
    k++;
  }
  double seconds = k * 1e-2;
  TEST_ASSERT_TRUE(seconds > 3.0 && seconds < 10.0);
  TEST_ASSERT_TRUE(s.t_h - s.t_t > 20.0 && s.t_h - s.t_t < 60.0); /* sensor reads above the tip */
}

static void test_thermocouple_reading(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  s.t_h = 125.0;
  s.t_t = 100.0;
  s.v_amp = 0.5;
  run(&s, 20, false); /* 40 sense time constants */
  double expected = P.amp_gain * (P.seebeck * (s.t_h - s.t_c) + P.amp_offset);
  NEAR(expected, s.v_amp, 0.005);
  TEST_ASSERT_TRUE(s.v_amp > 0.4 && s.v_amp < 0.6); /* about 250 * 21 uV * 100 K */
}

static void test_sense_node_clamped_while_heating_then_settles(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  run(&s, 100, true);
  NEAR(P.v_clamp, s.v_amp, 0.0);

  double prev = s.v_amp;
  for (int k = 1; k <= 3; k++) {
    t12_plant_step(&s, false, AMB);
    TEST_ASSERT_TRUE(s.v_amp < prev); /* falls monotonically */
    prev = s.v_amp;
    if (k == 1) { /* 1 ms = 2 time constants: the artifact is still about 0.4 V */
      double target1 = P.amp_gain * (P.seebeck * (s.t_h - s.t_c) + P.amp_offset);
      TEST_ASSERT_TRUE(s.v_amp - target1 > 0.3);
    }
  }
  double target = P.amp_gain * (P.seebeck * (s.t_h - s.t_c) + P.amp_offset);
  TEST_ASSERT_TRUE(s.v_amp - target > 0.001); /* an artifact is still visible at 3 ms */
  TEST_ASSERT_TRUE(s.v_amp - target < 0.02);  /* and it is small enough to average out */
}

static void test_healthy_current(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  t12_plant_step(&s, true, AMB);
  NEAR(3.0, s.i_heater, 1e-9);
  t12_plant_step(&s, false, AMB);
  NEAR(0.0, s.i_heater, 0.0);
}

static void test_open_tip(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  s.fault = T12_FAULT_OPEN;
  run(&s, 1000, true);
  NEAR(0.0, s.i_heater, 0.0);
  NEAR(AMB, s.t_t, 1e-9);
  NEAR(P.v_clamp, s.v_amp, 0.0);
}

static void test_shorted_tip(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  s.fault = T12_FAULT_SHORT;
  run(&s, 1000, true);
  NEAR(24.0, s.i_heater, 1e-9); /* far above the 3 A of a healthy tip */
  NEAR(AMB, s.t_t, 1e-9);
}

static void test_stuck_on_heats_when_commanded_off(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  s.fault = T12_FAULT_STUCK_ON;
  run(&s, 1000, false);
  NEAR(3.0, s.i_heater, 1e-9);
  TEST_ASSERT_TRUE(s.t_t > AMB + 20.0);
}

static void test_set_params_keeps_state_and_load_cools_the_tip(void)
{
  t12_plant_t s;
  t12_plant_init(&s, &P, 1e-3, AMB);
  run(&s, 2000, true);
  double t = s.t_t;
  t12_params_t touching = P;
  touching.g_load = 0.3;
  t12_plant_set_params(&s, &touching, 1e-3);
  NEAR(t, s.t_t, 0.0);

  t12_plant_t free_air;
  t12_plant_init(&free_air, &P, 1e-3, AMB);
  run(&free_air, 2000, true);
  run(&s, 2000, true);
  run(&free_air, 2000, true);
  TEST_ASSERT_TRUE(s.t_t < free_air.t_t - 10.0);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_steady_state);
  RUN_TEST(test_steady_state_with_load);
  RUN_TEST(test_energy_balance);
  RUN_TEST(test_cooling_matches_single_exponential);
  RUN_TEST(test_independent_of_step_size);
  RUN_TEST(test_default_tip_heats_in_a_few_seconds);
  RUN_TEST(test_thermocouple_reading);
  RUN_TEST(test_sense_node_clamped_while_heating_then_settles);
  RUN_TEST(test_healthy_current);
  RUN_TEST(test_open_tip);
  RUN_TEST(test_shorted_tip);
  RUN_TEST(test_stuck_on_heats_when_commanded_off);
  RUN_TEST(test_set_params_keeps_state_and_load_cools_the_tip);
  return UNITY_END();
}
