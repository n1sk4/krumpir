#ifndef T12_IO_H
#define T12_IO_H

#include <stdbool.h>
#include <stdint.h>

/* What the controller needs from the board. All readings are raw 12-bit ADC counts, so the
 * firmware does the same conversion on the simulated and the real front end. */
typedef struct {
  void (*heater_set)(void *ctx, bool on); /* Q1 gate: on = heat */
  uint16_t (*tc_read)(void *ctx);         /* thermocouple amplifier */
  uint16_t (*ntc_read)(void *ctx);        /* handle NTC divider */
  uint16_t (*current_read)(void *ctx);    /* heater current sense */
  void (*heartbeat_toggle)(void *ctx);    /* keeps Q3 on; call only while healthy */
  void (*safe_state)(void *ctx);          /* heater off */
  void *ctx;
} t12_io_t;

#endif
