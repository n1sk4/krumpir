#include "t12_convert.h"

#include <math.h>

#include "t12_hw.h"

#define KELVIN 273.15f

static float counts_to_volts(uint16_t counts)
{
  return (float)counts * (float)T12_ADC_VREF / (float)T12_ADC_MAX;
}

float t12_handle_celsius(uint16_t ntc)
{
  if (ntc == 0 || ntc >= T12_ADC_MAX)
    return NAN;
  float v = counts_to_volts(ntc);
  float r = (float)T12_NTC_R_FIXED * v / ((float)T12_ADC_VREF - v);
  float inv_t = 1.0f / 298.15f + logf(r / (float)T12_NTC_R25) / (float)T12_NTC_BETA;
  return 1.0f / inv_t - KELVIN;
}

float t12_tip_celsius(const t12_cal_t *cal, uint16_t tc, uint16_t ntc)
{
  if (tc >= T12_ADC_MAX)
    return NAN;
  float v_tc = counts_to_volts(tc) / cal->amp_gain - cal->offset_v;
  return v_tc / cal->seebeck + t12_handle_celsius(ntc);
}

float t12_heater_amps(uint16_t current)
{
  return counts_to_volts(current) / (float)(T12_SHUNT_OHM * T12_INA_GAIN);
}
