#ifndef T12_FRONTEND_H
#define T12_FRONTEND_H

#include <stdint.h>

/* The board's analog front end, physical values to the raw 12-bit ADC counts the controller reads
 * (t12_hw.h). Shared by the on-chip simulation and the HIL link, so both feed the controller the
 * same counts for the same plant. */
uint16_t t12_tc_counts(double v_amp);     /* thermocouple amplifier output [V] */
uint16_t t12_ntc_counts(double handle_c); /* NTC divider at this handle temperature */
uint16_t t12_current_counts(double amps); /* heater current through the shunt and INA */

#endif
