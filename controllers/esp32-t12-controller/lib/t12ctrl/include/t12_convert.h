#ifndef T12_CONVERT_H
#define T12_CONVERT_H

#include <stdint.h>

/* Raw ADC counts to physical values, in float (the control path stays off the double FPU).
 * NaN means there is no valid reading: an open or shorted NTC, or a saturated thermocouple
 * channel (the sense node is clamped while the heater is on or the tip is missing). */

/* The sensor chain is only nominal in the real world: resistor tolerance, the tip's Seebeck
 * coefficient and the amplifier offset all shift the reading. Calibrate these against a reference
 * thermometer; the defaults are the design values. */
typedef struct {
  float amp_gain;  /* 1 + Rf/Rg */
  float seebeck;   /* [V/K] of the thermocouple */
  float offset_v;  /* amplifier offset, input referred [V] */
} t12_cal_t;

#define T12_CAL_DEFAULT {250.0f, 21e-6f, 0.0f}

/* Handle temperature from the NTC divider (beta equation). */
float t12_handle_celsius(uint16_t ntc);

/* Temperature at the thermocouple junction: T = V_tc / seebeck + T_handle. */
float t12_tip_celsius(const t12_cal_t *cal, uint16_t tc, uint16_t ntc);

float t12_heater_amps(uint16_t current);

#endif
