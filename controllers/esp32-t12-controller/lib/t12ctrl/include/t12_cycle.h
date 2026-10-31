#ifndef T12_CYCLE_H
#define T12_CYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "t12_io.h"

/* One heater cycle, advanced once per control tick (1 ms):
 *   [ heat n ticks ][ settle ][ samples ][ idle until the period ends ]
 * The tip is only readable with the heater off, so the thermocouple is averaged over `samples`
 * ticks after `settle` ticks of switch-off, always right behind the heating phase. The duty set
 * during a cycle applies from the next one. n = duty * (period - settle - samples), rounded, so
 * the off window never gets shorter than settle + samples. */
#define T12_CYCLE_PERIOD_TICKS 100
#define T12_SETTLE_TICKS 3
#define T12_SAMPLE_TICKS 4

typedef struct {
  uint16_t tc;         /* thermocouple counts, averaged */
  uint16_t ntc;        /* handle NTC counts at the end of the window */
  uint16_t current;    /* heater current counts at the last heating tick */
  bool current_valid;  /* false when the cycle heated for fewer than two ticks */
  uint16_t heat_ticks; /* ticks the heater was on in this cycle */
} t12_meas_t;

typedef struct {
  uint32_t period, settle, samples, max_heat;
  uint32_t tick; /* position in the cycle */
  uint32_t heat; /* heating ticks of the running cycle */
  uint32_t acc, n;
  uint16_t current;
  float duty; /* 0..1, for the next cycle */
} t12_cycle_t;

void t12_cycle_init(t12_cycle_t *c, uint32_t period, uint32_t settle, uint32_t samples);
void t12_cycle_set_duty(t12_cycle_t *c, float duty); /* clamped to 0..1, NaN counts as 0 */

/* Drives the heater through io and returns true on the tick a new measurement is complete. */
bool t12_cycle_tick(t12_cycle_t *c, const t12_io_t *io, t12_meas_t *meas);

#endif
