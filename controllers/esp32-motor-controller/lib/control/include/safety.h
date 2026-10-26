#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

typedef enum { FAULT_NONE = 0, FAULT_OVERSPEED, FAULT_SENSOR_TIMEOUT } fault_t;

typedef struct {
  float max_speed_rpm;        /* fault when |speed| exceeds this */
  uint32_t max_invalid_ticks; /* fault after this many consecutive non-finite readings */
} safety_cfg_t;

/* The N20 gearmotor model has a no-load speed of about 5600 rpm. */
#define SAFETY_CFG_DEFAULT {7000.0f, 50}

/* Latching supervisor: the first fault stays until safety_clear(). */
typedef struct {
  safety_cfg_t cfg;
  uint32_t invalid_ticks;
  fault_t latched;
} safety_t;

void safety_init(safety_t *s, const safety_cfg_t *cfg);

/* Call once per control cycle with the measured speed. Returns the latched fault, or
 * FAULT_NONE. A single bad reading is not a fault; the controller must tolerate it. */
fault_t safety_update(safety_t *s, float speed_rpm);

void safety_clear(safety_t *s);
const char *fault_name(fault_t f);

#endif
