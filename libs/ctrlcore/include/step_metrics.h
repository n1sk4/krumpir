#ifndef STEP_METRICS_H
#define STEP_METRICS_H

#include <stdint.h>

/* Step-response figures computed from a stream of samples taken every dt seconds.
 * Feed it the samples that follow a setpoint change or a disturbance. */
typedef struct {
  float target;
  float band; /* settling band as a fraction of |target| */
  float dt;
  float min, max, last;
  uint32_t n;
  uint32_t last_outside; /* index of the last sample outside the band */
  uint32_t any_outside;  /* non-zero once a sample has been outside the band */
} step_metrics_t;

void step_metrics_init(step_metrics_t *m, float target, float band_pct, float dt);
void step_metrics_add(step_metrics_t *m, float value);

float step_metrics_overshoot_pct(const step_metrics_t *m);  /* above target, 0 if none */
float step_metrics_undershoot_pct(const step_metrics_t *m); /* below target, 0 if none */
float step_metrics_settling_s(const step_metrics_t *m);     /* 0 if always inside the band */
float step_metrics_final_error(const step_metrics_t *m);    /* target - last sample */

#endif
