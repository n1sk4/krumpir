#ifndef CTRL_PARAMS_H
#define CTRL_PARAMS_H

#include <stdbool.h>

/* Runtime-adjustable controller settings, handed from the console to the control task. */
typedef struct {
  float sp; /* setpoint [rpm] */
  float kp, ki, kd;
  bool run;
} ctrl_params_t;

#define CTRL_SP_MAX_RPM 10000.0f
#define CTRL_GAIN_MAX 1000000.0f
/* Boot state: stopped, with the tuned gains (method and results in docs/tuning.md). */
#define CTRL_PARAMS_DEFAULT {0.0f, 7.2e-4f, 0.03f, 0.0f, false}

#endif
