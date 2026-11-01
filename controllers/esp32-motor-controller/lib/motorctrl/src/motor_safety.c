#include "motor_safety.h"

#include <math.h>

void motor_safety_init(motor_safety_t *s, const motor_safety_cfg_t *cfg)
{
  s->cfg = *cfg;
  motor_safety_clear(s);
}

void motor_safety_clear(motor_safety_t *s)
{
  s->invalid_ticks = 0;
  s->latched = MOTOR_TRIP_NONE;
}

motor_trip_t motor_safety_update(motor_safety_t *s, float speed_rpm)
{
  if (s->latched != MOTOR_TRIP_NONE)
    return s->latched;

  if (!isfinite(speed_rpm)) {
    if (++s->invalid_ticks >= s->cfg.max_invalid_ticks)
      s->latched = MOTOR_TRIP_SENSOR;
  } else {
    s->invalid_ticks = 0;
    if (fabsf(speed_rpm) > s->cfg.max_speed_rpm)
      s->latched = MOTOR_TRIP_OVERSPEED;
  }
  return s->latched;
}

const char *motor_trip_name(motor_trip_t t)
{
  switch (t) {
  case MOTOR_TRIP_NONE:
    return "none";
  case MOTOR_TRIP_OVERSPEED:
    return "overspeed";
  case MOTOR_TRIP_SENSOR:
    return "sensor timeout";
  }
  return "unknown";
}
