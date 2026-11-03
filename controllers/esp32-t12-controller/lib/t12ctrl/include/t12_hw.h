#ifndef T12_HW_H
#define T12_HW_H

/* Board constants shared by the simulated front end and the conversion math. Values follow
 * docs/T12 Controller Design.md and an ideal 12-bit ADC (no eFuse nonlinearity). The NTC divider
 * is a placeholder until the board is built. */
#define T12_ADC_MAX 4095            /* 12-bit counts */
#define T12_ADC_VREF 3.3            /* full scale [V] */
#define T12_SHUNT_OHM 0.010         /* heater current shunt */
#define T12_INA_GAIN 50.0           /* INA240A2: 3 A gives 1.5 V */
#define T12_NTC_R25 100e3           /* [ohm] at 25 degC */
#define T12_NTC_BETA 3950.0         /* [K] */
#define T12_NTC_R_FIXED 100e3       /* divider resistor to 3V3; the NTC goes to ground [ohm] */
#define T12_HEARTBEAT_TIMEOUT_S 0.1 /* Q3 drops this long after the last heartbeat toggle */
#define T12_NTC_T25_K 298.15        /* 25 degC [K], the NTC reference */
#define T12_KELVIN 273.15           /* 0 degC [K] */

#endif
