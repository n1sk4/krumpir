#include <math.h>
#include <unity.h>

#include "t12_convert.h"
#include "t12_hw.h"
#include "t12_io_sim.h"

#define NEAR(expected, actual, tol) near_((expected), (actual), (tol), #actual)

static const t12_params_t P = T12_PARAMS_DEFAULT;
static const t12_cal_t CAL = T12_CAL_DEFAULT;

void setUp(void) {}
void tearDown(void) {}

static void near_(double expected, double actual, double tol, const char *what)
{
  TEST_ASSERT_TRUE_MESSAGE(fabs(expected - actual) <= tol, what);
}

/* A tip held at the given temperatures with the heater off, settled long enough to read. */
static void rig(t12_io_sim_t *s, t12_io_t *io, double t_h, double t_c)
{
  t12_io_sim_init(s, io, &P, 1e-3, 25.0);
  s->plant.t_h = s->plant.t_t = t_h;
  s->plant.t_c = t_c;
  s->plant.v_amp = 0.0;
  s->plant.series_on = true;
}

static void test_ntc_at_25_degrees(void)
{
  NEAR(25.0, t12_handle_celsius(2048), 0.1);
}

static void test_ntc_round_trip_through_the_front_end(void)
{
  static const double temps[] = {0.0, 25.0, 40.0, 60.0, 100.0};
  for (unsigned i = 0; i < sizeof temps / sizeof temps[0]; i++) {
    t12_io_sim_t s;
    t12_io_t io;
    t12_io_sim_init(&s, &io, &P, 1e-3, 25.0);
    s.plant.t_c = temps[i];
    NEAR(temps[i], t12_handle_celsius(io.ntc_read(io.ctx)), 0.5);
  }
}

static void test_ntc_open_or_shorted_is_invalid(void)
{
  TEST_ASSERT_TRUE(isnan(t12_handle_celsius(0)));
  TEST_ASSERT_TRUE(isnan(t12_handle_celsius(T12_ADC_MAX)));
}

/* Holds the tip temperatures while the sense node settles, so only the front end is tested. */
static void settle_node(t12_io_sim_t *s, double t_h, double t_c)
{
  for (int k = 0; k < 20; k++) {
    s->plant.t_h = t_h;
    s->plant.t_c = t_c;
    t12_io_sim_step(s);
  }
}

static void test_tip_temperature_round_trip(void)
{
  static const double tips[] = {100.0, 250.0, 350.0, 450.0};
  for (unsigned i = 0; i < sizeof tips / sizeof tips[0]; i++) {
    t12_io_sim_t s;
    t12_io_t io;
    rig(&s, &io, tips[i], 40.0);
    settle_node(&s, tips[i], 40.0);
    float t = t12_tip_celsius(&CAL, io.tc_read(io.ctx), io.ntc_read(io.ctx));
    NEAR(tips[i], t, 1.0); /* 0.15 degC of ADC step plus 0.5 degC of amplifier offset */
  }
}

static void test_calibrated_offset_removes_the_amplifier_error(void)
{
  t12_io_sim_t s;
  t12_io_t io;
  rig(&s, &io, 350.0, 40.0);
  settle_node(&s, 350.0, 40.0);
  t12_cal_t cal = CAL;
  cal.offset_v = 10e-6f; /* the plant's amplifier offset */
  NEAR(350.0, t12_tip_celsius(&cal, io.tc_read(io.ctx), io.ntc_read(io.ctx)), 0.4);
}

static void test_calibration_gain_scales_the_thermocouple_part(void)
{
  t12_cal_t cal = CAL;
  cal.amp_gain = 252.5f; /* 1 % more gain reads 1 % less thermocouple voltage */
  uint16_t tc = 2000, ntc = 2048;
  float nominal = t12_tip_celsius(&CAL, tc, ntc) - t12_handle_celsius(ntc);
  float scaled = t12_tip_celsius(&cal, tc, ntc) - t12_handle_celsius(ntc);
  NEAR(nominal * 0.99, scaled, nominal * 1e-3);
}

static void test_saturated_thermocouple_is_invalid(void)
{
  TEST_ASSERT_TRUE(isnan(t12_tip_celsius(&CAL, T12_ADC_MAX, 2048)));
}

static void test_heater_current(void)
{
  NEAR(3.0, t12_heater_amps(1861), 0.01);
  NEAR(0.0, t12_heater_amps(0), 0.0);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_ntc_at_25_degrees);
  RUN_TEST(test_ntc_round_trip_through_the_front_end);
  RUN_TEST(test_ntc_open_or_shorted_is_invalid);
  RUN_TEST(test_tip_temperature_round_trip);
  RUN_TEST(test_calibrated_offset_removes_the_amplifier_error);
  RUN_TEST(test_calibration_gain_scales_the_thermocouple_part);
  RUN_TEST(test_saturated_thermocouple_is_invalid);
  RUN_TEST(test_heater_current);
  return UNITY_END();
}
