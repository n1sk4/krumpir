#ifndef T12_SCENARIO_H
#define T12_SCENARIO_H

#include "t12_ctrl.h"
#include "t12_safety.h"

/* Closed-loop scenarios on the simulated tip, 350 degC setpoint, 25 degC ambient, default plant:
 *   COLD_START  heat from cold and hold
 *   LOAD_STEP   at 30 s a joint draws about 32 W (0.1 W/K)
 *   AMBIENT     at 30 s the room drops to 10 degC
 *   OPEN_TIP    at 10 s the tip is pulled out
 *   SHORTED_TIP at 10 s the tip shorts
 *   STUCK       at 10 s the heater switch sticks on
 *   RUNAWAY     at 10 s the tip loses more heat (0.5 W/K) than the supply can make up */
typedef enum {
  T12_SCN_COLD_START,
  T12_SCN_LOAD_STEP,
  T12_SCN_AMBIENT_STEP,
  T12_SCN_OPEN_TIP,
  T12_SCN_SHORTED_TIP,
  T12_SCN_STUCK_SWITCH,
  T12_SCN_RUNAWAY,
  T12_SCN_COUNT
} t12_scenario_t;

/* One row per measurement (10 Hz). tip and sensor are the true temperatures of the model. */
typedef struct {
  float t_s, setpoint, temp, tip, sensor, handle, duty, amps, load, ambient;
  t12_trip_t trip;
} t12_scn_sample_t;

/* Figures for the response after the event (the whole run for COLD_START), against 350 degC with a
 * 2 % band. The trip fields are for the fault scenarios. */
typedef struct {
  float overshoot_pct;
  float undershoot_pct; /* dip below the target */
  float settling_s;
  float final_error;
  t12_trip_t trip; /* first trip, or NONE */
  float trip_s;    /* time from the event to the trip, -1 if none */
} t12_scn_result_t;

typedef void (*t12_scn_sink_t)(void *ctx, const t12_scn_sample_t *s);

const char *t12_scenario_name(t12_scenario_t id);

/* Runs one scenario with the given controller settings. sink may be NULL. */
t12_scn_result_t t12_scenario_run(t12_scenario_t id, const t12_ctrl_cfg_t *ctrl,
                                  t12_scn_sink_t sink, void *ctx);

#endif
