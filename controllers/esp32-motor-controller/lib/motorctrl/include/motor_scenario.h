#ifndef MOTOR_SCENARIO_H
#define MOTOR_SCENARIO_H

#include "motor_ctrl.h"

/* Closed-loop scenarios on the simulated motor at 1 kHz, run through motor_loop:
 *   STEP           setpoint 0 -> 3000 rpm
 *   LOAD_STEP      3000 rpm at steady state, then a 0.001 N*m load torque is applied
 *   SATURATION     setpoint 10000 rpm (above the no-load speed) until saturated, then 3000 rpm
 *   HEAVY_INERTIA  STEP with twice the rotor inertia */
typedef enum {
  MOTOR_SCN_STEP,
  MOTOR_SCN_LOAD_STEP,
  MOTOR_SCN_SATURATION,
  MOTOR_SCN_HEAVY_INERTIA,
  MOTOR_SCN_COUNT
} motor_scenario_t;

typedef struct {
  float t_s, setpoint_rpm, speed_rpm, duty, load_nm;
} motor_scn_sample_t;

/* Figures for the response after the setpoint change or disturbance (3000 rpm target,
 * 2 % settling band). */
typedef struct {
  float overshoot_pct;
  float undershoot_pct; /* dip below the target */
  float settling_s;
  float final_error_rpm;
  float min_rpm;
  float duty_saturated; /* duty while saturated (SATURATION only, else 0) */
} motor_scn_result_t;

typedef void (*motor_scn_sink_t)(void *ctx, const motor_scn_sample_t *s);

const char *motor_scenario_name(motor_scenario_t id);

/* Runs one scenario with the given controller settings. sink may be NULL. */
motor_scn_result_t motor_scenario_run(motor_scenario_t id, const motor_ctrl_cfg_t *ctrl,
                                      motor_scn_sink_t sink, void *ctx);

#endif
