#include <math.h>
#include <unity.h>

#include "t12_hw.h"
#include "t12_io_sim.h"

#define AMB 25.0
#define NEAR(expected, actual, tol) near_((expected), (actual), (tol), #actual)

static const t12_params_t P = T12_PARAMS_DEFAULT;
static const unsigned CURRENT_3A = 1861; /* 3 A * 10 mohm * 50 = 1.5 V of 3.3 V, 12 bit */

void setUp(void) {}
void tearDown(void) {}

static void near_(double expected, double actual, double tol, const char *what)
{
  TEST_ASSERT_TRUE_MESSAGE(fabs(expected - actual) <= tol, what);
}

static void run(t12_io_sim_t *s, t12_io_t *io, int steps, bool heat, bool beat)
{
  for (int k = 0; k < steps; k++) {
    io->heater_set(io->ctx, heat);
    if (beat)
      io->heartbeat_toggle(io->ctx);
    t12_io_sim_step(s);
  }
}

static void test_idle_readings(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  NEAR(3, io.tc_read(io.ctx), 1);       /* 250 * 10 uV of offset */
  NEAR(2047.5, io.ntc_read(io.ctx), 1); /* 100 k NTC at 25 degC halves the divider */
  TEST_ASSERT_EQUAL_UINT16(0, io.current_read(io.ctx));
}

static void test_ntc_counts_fall_as_the_handle_warms(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  s.plant.t_c = 60.0;
  double r = T12_NTC_R25 * exp(T12_NTC_BETA * (1.0 / 333.15 - 1.0 / 298.15));
  NEAR(T12_ADC_MAX * r / (r + T12_NTC_R_FIXED), io.ntc_read(io.ctx), 1);
  TEST_ASSERT_TRUE(io.ntc_read(io.ctx) < 2000);
}

static void test_command_applies_at_the_next_read(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  io.heater_set(io.ctx, true);
  TEST_ASSERT_EQUAL_UINT16(0, io.current_read(io.ctx)); /* not applied yet */
  t12_io_sim_step(&s);
  NEAR(CURRENT_3A, io.current_read(io.ctx), 1);
  TEST_ASSERT_EQUAL_UINT16(T12_ADC_MAX, io.tc_read(io.ctx)); /* sense node clamped while heating */
}

static void test_short_saturates_the_current_reading(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  s.plant.fault = T12_FAULT_SHORT;
  run(&s, &io, 5, true, true);
  TEST_ASSERT_EQUAL_UINT16(T12_ADC_MAX, io.current_read(io.ctx));
}

static void test_missing_heartbeat_drops_the_heater(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  run(&s, &io, 300, true, true);
  NEAR(CURRENT_3A, io.current_read(io.ctx), 1);
  run(&s, &io, 50, true, false);
  NEAR(CURRENT_3A, io.current_read(io.ctx), 1); /* still inside the 100 ms timeout */
  run(&s, &io, 51, true, false);
  TEST_ASSERT_EQUAL_UINT16(0, io.current_read(io.ctx));
  run(&s, &io, 1, true, true); /* recovers once the heartbeat is back */
  NEAR(CURRENT_3A, io.current_read(io.ctx), 1);
}

static void test_heartbeat_cuts_a_stuck_switch(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  s.plant.fault = T12_FAULT_STUCK_ON;
  run(&s, &io, 50, false, true);
  NEAR(CURRENT_3A, io.current_read(io.ctx), 1); /* current although commanded off */
  run(&s, &io, 102, false, false);
  TEST_ASSERT_EQUAL_UINT16(0, io.current_read(io.ctx));
}

static void test_safe_state_turns_the_heater_off(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  t12_io_sim_init(&s, &io, &P, 1e-3, AMB);
  run(&s, &io, 10, true, true);
  io.safe_state(io.ctx);
  t12_io_sim_step(&s);
  TEST_ASSERT_EQUAL_UINT16(0, io.current_read(io.ctx));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_idle_readings);
  RUN_TEST(test_ntc_counts_fall_as_the_handle_warms);
  RUN_TEST(test_command_applies_at_the_next_read);
  RUN_TEST(test_short_saturates_the_current_reading);
  RUN_TEST(test_missing_heartbeat_drops_the_heater);
  RUN_TEST(test_heartbeat_cuts_a_stuck_switch);
  RUN_TEST(test_safe_state_turns_the_heater_off);
  return UNITY_END();
}
