#include "t12_frontend.h"

#include <math.h>

#include "t12_hw.h"

static uint16_t to_counts(double volts)
{
  double c = floor(volts / T12_ADC_VREF * T12_ADC_MAX + 0.5);
  return (uint16_t)(c < 0 ? 0 : c > T12_ADC_MAX ? T12_ADC_MAX : c);
}

uint16_t t12_tc_counts(double v_amp) { return to_counts(v_amp); }

uint16_t t12_ntc_counts(double handle_c)
{
  double t_k = handle_c + T12_KELVIN;
  double r = T12_NTC_R25 * exp(T12_NTC_BETA * (1.0 / t_k - 1.0 / T12_NTC_T25_K));
  return to_counts(T12_ADC_VREF * r / (r + T12_NTC_R_FIXED));
}

uint16_t t12_current_counts(double amps) { return to_counts(amps * T12_SHUNT_OHM * T12_INA_GAIN); }
