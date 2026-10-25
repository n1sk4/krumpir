#include "safety.h"

#include <math.h>

void safety_init(safety_t *s, const safety_cfg_t *cfg)
{
    s->cfg = *cfg;
    safety_clear(s);
}

void safety_clear(safety_t *s)
{
    s->invalid_ticks = 0;
    s->latched = FAULT_NONE;
}

fault_t safety_update(safety_t *s, float speed_rpm)
{
    if (s->latched != FAULT_NONE) return s->latched;

    if (!isfinite(speed_rpm)) {
        if (++s->invalid_ticks >= s->cfg.max_invalid_ticks) s->latched = FAULT_SENSOR_TIMEOUT;
    } else {
        s->invalid_ticks = 0;
        if (fabsf(speed_rpm) > s->cfg.max_speed_rpm) s->latched = FAULT_OVERSPEED;
    }
    return s->latched;
}

const char *fault_name(fault_t f)
{
    switch (f) {
    case FAULT_NONE: return "none";
    case FAULT_OVERSPEED: return "overspeed";
    case FAULT_SENSOR_TIMEOUT: return "sensor timeout";
    }
    return "unknown";
}
