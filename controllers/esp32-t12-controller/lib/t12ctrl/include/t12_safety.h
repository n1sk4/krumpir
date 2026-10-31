#ifndef T12_SAFETY_H
#define T12_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#include "t12_cycle.h"

/* Latching supervisor, called once per measurement (once per heater cycle). The first trip stays
 * until t12_safety_clear(). The heartbeat that keeps the series switch on must only run while
 * t12_safety_ok() is true, so a trip also drops the second switch within 100 ms. */
typedef enum {
  T12_TRIP_NONE = 0,
  T12_TRIP_OVER_TEMP,  /* a reading above max_temp */
  T12_TRIP_STUCK_ON,   /* current while the heater is commanded off */
  T12_TRIP_SHORT,      /* heater loop resistance below r_min */
  T12_TRIP_NO_TIP,     /* no current while heating: missing tip or open loop */
  T12_TRIP_RESISTANCE, /* heater loop resistance above r_max */
  T12_TRIP_SENSOR,     /* too many invalid temperature readings in a row */
  T12_TRIP_RUNAWAY,    /* full power for too long without a temperature rise */
} t12_trip_t;

typedef struct {
  float max_temp;           /* [degC] */
  float runaway_duty;       /* duty counted as full power */
  uint32_t runaway_cycles;  /* cycles of full power allowed without the rise below */
  float runaway_rise;       /* [K] */
  uint32_t max_invalid;     /* consecutive invalid readings tolerated minus one */
  float supply_v;           /* nominal heater supply [V] */
  float r_min, r_max;       /* healthy heater loop resistance [ohm] */
  float min_heat_amps;      /* less than this while heating means no tip [A] */
  float max_off_amps;       /* more than this while commanded off means a stuck switch [A] */
} t12_safety_cfg_t;

/* From docs/T12 Controller Design.md: 480 degC limit, 20 s at full power without a 50 K rise,
 * 6 to 10 ohm. The cycle is 0.1 s, so 200 cycles are 20 s. */
#define T12_SAFETY_CFG_DEFAULT {480.0f, 0.9f, 200, 50.0f, 5, 24.0f, 6.0f, 10.0f, 0.3f, 0.5f}

typedef struct {
  t12_safety_cfg_t cfg;
  uint32_t invalid;
  uint32_t full_cycles;
  float ref_temp; /* temperature at the start of the full-power window */
  t12_trip_t latched;
} t12_safety_t;

void t12_safety_init(t12_safety_t *s, const t12_safety_cfg_t *cfg);

/* temp is the converted measurement (NaN when invalid). Returns the latched trip or NONE. A single
 * invalid reading is not a trip; the controller holds its last duty meanwhile. */
t12_trip_t t12_safety_update(t12_safety_t *s, const t12_meas_t *m, float temp);

bool t12_safety_ok(const t12_safety_t *s); /* no trip latched */
void t12_safety_clear(t12_safety_t *s);
const char *t12_trip_name(t12_trip_t t);

#endif
