#include <math.h>
#include <unity.h>

#include "plant_sim.h"

static const plant_params_t P = PLANT_PARAMS_N20;
static const double DEN = 2.5e-6 + 1e-4; /* R*b + Kt*Ke */

void setUp(void) {}
void tearDown(void) {}

static void run(plant_sim_t *s, int steps, float volts, float torque)
{
  for (int k = 0; k < steps; k++)
    plant_sim_step(s, volts, torque);
}

static void test_steady_state(void)
{
  plant_sim_t s;
  plant_sim_init(&s, &P, 1e-3f);
  run(&s, 1000, 6.0f, 0.0f);
  double w_ss = 6.0 * 0.01 / DEN;
  TEST_ASSERT_FLOAT_WITHIN((float)(w_ss * 1e-3), (float)(w_ss), s.w);
}

static void test_step_response_matches_analytic(void)
{
  double a = 2.5 / 1e-3 + 1e-6 / 1e-6; /* R/L + b/J */
  double c = DEN / (1e-3 * 1e-6);      /* (R*b + Kt*Ke) / (L*J) */
  double d = sqrt(a * a - 4 * c);
  double p1 = (-a + d) / 2, p2 = (-a - d) / 2;
  double w_ss = 6.0 * 0.01 / DEN;

  plant_sim_t s;
  plant_sim_init(&s, &P, 1e-3f);
  for (int k = 1; k <= 200; k++) {
    plant_sim_step(&s, 6.0f, 0.0f);
    if (k == 5 || k == 20 || k == 50 || k == 100 || k == 200) {
      double t = k * 1e-3;
      double w = w_ss * (1 + (p2 * exp(p1 * t) - p1 * exp(p2 * t)) / (p1 - p2));
      TEST_ASSERT_FLOAT_WITHIN((float)(w_ss * 0.01), (float)(w), s.w);
    }
  }
}

static void test_independent_of_step_size(void)
{
  plant_sim_t coarse, fine;
  plant_sim_init(&coarse, &P, 1e-3f);
  plant_sim_init(&fine, &P, 1e-4f);
  run(&coarse, 30, 6.0f, 0.0f);
  run(&fine, 300, 6.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(fine.w * 5e-3f, fine.w, coarse.w);
  TEST_ASSERT_FLOAT_WITHIN(fine.i * 5e-3f, fine.i, coarse.i);
}

static void test_load_torque(void)
{
  plant_sim_t s;
  plant_sim_init(&s, &P, 1e-3f);
  run(&s, 1000, 6.0f, 0.001f);
  double w_ss = (6.0 * 0.01 - 2.5 * 0.001) / DEN;
  TEST_ASSERT_FLOAT_WITHIN((float)(w_ss * 1e-3), (float)(w_ss), s.w);
}

static void test_set_params_keeps_state_and_reset_zeroes(void)
{
  plant_sim_t s;
  plant_sim_init(&s, &P, 1e-3f);
  run(&s, 20, 6.0f, 0.0f);
  float w = s.w, i = s.i;
  plant_params_t heavy = P;
  heavy.J *= 2.0f;
  plant_sim_set_params(&s, &heavy, 1e-3f);
  TEST_ASSERT_EQUAL_FLOAT(w, s.w);
  TEST_ASSERT_EQUAL_FLOAT(i, s.i);
  plant_sim_reset(&s);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.w);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.i);
}

static void test_coeffs_path_matches_set_params(void)
{
  plant_params_t heavy = P;
  heavy.J *= 2.0f;
  plant_sim_t a, b;
  plant_sim_init(&a, &P, 1e-3f);
  plant_sim_init(&b, &P, 1e-3f);
  run(&a, 10, 6.0f, 0.0f);
  run(&b, 10, 6.0f, 0.0f);
  plant_coeffs_t c;
  plant_sim_set_params(&a, &heavy, 1e-3f);
  plant_sim_coeffs(&heavy, 1e-3f, &c);
  plant_sim_set_coeffs(&b, &c);
  run(&a, 50, 6.0f, 0.0f);
  run(&b, 50, 6.0f, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(a.w, b.w);
  TEST_ASSERT_EQUAL_FLOAT(a.i, b.i);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_steady_state);
  RUN_TEST(test_step_response_matches_analytic);
  RUN_TEST(test_independent_of_step_size);
  RUN_TEST(test_load_torque);
  RUN_TEST(test_set_params_keeps_state_and_reset_zeroes);
  RUN_TEST(test_coeffs_path_matches_set_params);
  return UNITY_END();
}
