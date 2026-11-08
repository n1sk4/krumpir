#ifndef MOTOR_SAFETY_H
#define MOTOR_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

/* Latching supervisor, called once per control tick. The first trip stays until
 * motor_safety_clear(); a trip stops the controller (motor_loop). */
typedef enum {
  MOTOR_TRIP_NONE = 0,
  MOTOR_TRIP_OVERSPEED, /* |speed| above max_speed_rpm */
  MOTOR_TRIP_SENSOR,    /* too many invalid speed readings in a row */
  MOTOR_TRIP_STALL,     /* driven hard but not turning: a dead encoder (it reads 0) or a jam */
} motor_trip_t;

typedef struct {
  float max_speed_rpm;        /* trip when |speed| exceeds this */
  uint32_t max_invalid_ticks; /* trip after this many consecutive non-finite readings */
  float stall_duty;           /* |duty| at or above this counts as driving hard */
  float stall_rpm;            /* |speed| below this counts as not turning */
  uint32_t max_stall_ticks;   /* trip after this many consecutive ticks of both */
} motor_safety_cfg_t;

/* The N20 gearmotor model has a no-load speed of about 5600 rpm, about 5590 rpm per unit duty and a
 * 24 ms mechanical time constant: at half duty it passes 100 rpm within a millisecond, so 200 ms
 * of half duty or more below 100 rpm means the shaft or its encoder is not there. */
#define MOTOR_SAFETY_CFG_DEFAULT {7000.0f, 50, 0.5f, 100.0f, 200}

typedef struct {
  motor_safety_cfg_t cfg;
  uint32_t invalid_ticks;
  uint32_t stall_ticks;
  motor_trip_t latched;
} motor_safety_t;

void motor_safety_init(motor_safety_t *s, const motor_safety_cfg_t *cfg);
/* Returns the latched trip, or MOTOR_TRIP_NONE. duty is the command applied while speed_rpm was
 * measured (the previous tick's). A single bad reading is not a trip; the controller must
 * tolerate it. */
motor_trip_t motor_safety_update(motor_safety_t *s, float speed_rpm, float duty);
void motor_safety_clear(motor_safety_t *s);
static inline bool motor_safety_ok(const motor_safety_t *s) { return s->latched == MOTOR_TRIP_NONE; }
const char *motor_trip_name(motor_trip_t t);

#endif
