#ifndef SCENARIO_H
#define SCENARIO_H

#include "ctrl_params.h"

/* Closed-loop test scenarios: PID + simulated motor at 1 kHz.
 *   STEP           setpoint 0 -> 3000 rpm
 *   LOAD_STEP      3000 rpm at steady state, then a 0.001 N*m load torque is applied
 *   SATURATION     setpoint 10000 rpm (above the no-load speed) until saturated, then 3000 rpm
 *   HEAVY_INERTIA  STEP with twice the rotor inertia */
typedef enum { SCN_STEP, SCN_LOAD_STEP, SCN_SATURATION, SCN_HEAVY_INERTIA, SCN_COUNT } scenario_t;

typedef struct {
  float t_s, setpoint_rpm, speed_rpm, duty, load_nm;
} scn_sample_t;

/* Figures for the response after the setpoint change or disturbance (3000 rpm target,
 * 2 % settling band). */
typedef struct {
  float overshoot_pct;
  float undershoot_pct; /* dip below the target */
  float settling_s;
  float final_error_rpm;
  float min_rpm;
  float duty_saturated; /* duty while saturated (SATURATION only, else 0) */
} scn_result_t;

typedef void (*scn_sink_t)(void *ctx, const scn_sample_t *s);

const char *scenario_name(scenario_t id);

/* Runs one scenario with the given gains (kp, ki, kd are used). sink may be NULL. */
scn_result_t scenario_run(scenario_t id, const ctrl_params_t *gains, scn_sink_t sink, void *ctx);

#endif
