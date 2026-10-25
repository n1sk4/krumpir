#include "step_metrics.h"

#include <math.h>

void step_metrics_init(step_metrics_t *m, float target, float band_pct, float dt)
{
    m->target = target;
    m->band = band_pct / 100.0f;
    m->dt = dt;
    m->min = INFINITY;
    m->max = -INFINITY;
    m->last = 0.0f;
    m->n = 0;
    m->last_outside = 0;
    m->any_outside = 0;
}

void step_metrics_add(step_metrics_t *m, float value)
{
    if (value < m->min) m->min = value;
    if (value > m->max) m->max = value;
    if (fabsf(value - m->target) > m->band * fabsf(m->target)) {
        m->last_outside = m->n;
        m->any_outside = 1;
    }
    m->last = value;
    m->n++;
}

float step_metrics_overshoot_pct(const step_metrics_t *m)
{
    float over = m->max - m->target;
    return over > 0.0f ? 100.0f * over / fabsf(m->target) : 0.0f;
}

float step_metrics_undershoot_pct(const step_metrics_t *m)
{
    float under = m->target - m->min;
    return under > 0.0f ? 100.0f * under / fabsf(m->target) : 0.0f;
}

float step_metrics_settling_s(const step_metrics_t *m)
{
    return m->any_outside ? (float)(m->last_outside + 1) * m->dt : 0.0f;
}

float step_metrics_final_error(const step_metrics_t *m)
{
    return m->target - m->last;
}
