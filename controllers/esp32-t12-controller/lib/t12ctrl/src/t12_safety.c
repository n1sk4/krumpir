#include "t12_safety.h"

#include <math.h>

#include "t12_convert.h"

void t12_safety_init(t12_safety_t *s, const t12_safety_cfg_t *cfg)
{
  s->cfg = *cfg;
  t12_safety_clear(s);
}

void t12_safety_clear(t12_safety_t *s)
{
  s->invalid = 0;
  s->full_cycles = 0;
  s->ref_temp = 0.0f;
  s->latched = T12_TRIP_NONE;
}

bool t12_safety_ok(const t12_safety_t *s) { return s->latched == T12_TRIP_NONE; }

static t12_trip_t check(t12_safety_t *s, const t12_meas_t *m, float temp)
{
  const t12_safety_cfg_t *c = &s->cfg;

  if (isfinite(temp) && temp > c->max_temp)
    return T12_TRIP_OVER_TEMP;

  if (t12_heater_amps(m->current_off) > c->max_off_amps)
    return T12_TRIP_STUCK_ON;

  if (m->current_valid) {
    float amps = t12_heater_amps(m->current);
    if (amps < c->min_heat_amps)
      return T12_TRIP_NO_TIP;
    float r = c->supply_v / amps;
    if (r < c->r_min)
      return T12_TRIP_SHORT;
    if (r > c->r_max)
      return T12_TRIP_RESISTANCE;
  }

  if (!isfinite(temp)) {
    if (++s->invalid >= c->max_invalid)
      return T12_TRIP_SENSOR;
    return T12_TRIP_NONE; /* no rise information from a bad reading */
  }
  s->invalid = 0;

  if (m->duty < c->runaway_duty || s->full_cycles == 0) {
    s->ref_temp = temp; /* a new window starts at the first full-power cycle */
    s->full_cycles = m->duty >= c->runaway_duty ? 1 : 0;
  } else if (temp - s->ref_temp >= c->runaway_rise) {
    s->ref_temp = temp;
    s->full_cycles = 1;
  } else if (++s->full_cycles >= c->runaway_cycles) {
    return T12_TRIP_RUNAWAY;
  }
  return T12_TRIP_NONE;
}

t12_trip_t t12_safety_update(t12_safety_t *s, const t12_meas_t *m, float temp)
{
  if (s->latched == T12_TRIP_NONE)
    s->latched = check(s, m, temp);
  return s->latched;
}

const char *t12_trip_name(t12_trip_t t)
{
  switch (t) {
  case T12_TRIP_NONE: return "none";
  case T12_TRIP_OVER_TEMP: return "over temperature";
  case T12_TRIP_STUCK_ON: return "heater stuck on";
  case T12_TRIP_SHORT: return "heater short";
  case T12_TRIP_NO_TIP: return "no tip";
  case T12_TRIP_RESISTANCE: return "heater resistance";
  case T12_TRIP_SENSOR: return "sensor timeout";
  case T12_TRIP_RUNAWAY: return "thermal runaway";
  }
  return "unknown";
}
