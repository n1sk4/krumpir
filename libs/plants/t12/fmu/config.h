#ifndef config_h
#define config_h

/* T12 tip plant (libs/plants/t12) as an FMI co-simulation FMU, Reference FMUs framework. */

#include <stdbool.h>

#include "t12_plant.h"

#define MODEL_IDENTIFIER t12tip
#define INSTANTIATION_TOKEN "{0d3c7e91-5a24-4b6f-8e1d-7c9a2f4b6e02}"

#define CO_SIMULATION

#define GET_FLOAT64
#define SET_FLOAT64
#define EVENT_UPDATE

/* The plant is discretized exactly for this step: one plant step per solver step (1 ms). */
#define FIXED_SOLVER_STEP 0.001
#define DEFAULT_STOP_TIME 10

/* Value references, as in FMI3.xml and FMI2.xml */
typedef enum {
  vr_time,
  vr_heater,     /* input: heater switch Q1, on when >= 0.5 */
  vr_series_on,  /* input: series switch Q3 (heartbeat), on when >= 0.5 */
  vr_ambient_c,  /* input [degC] */
  vr_load_wk,    /* input: extra tip-to-ambient conductance of touching a joint [W/K] */
  vr_fault,      /* input: 0 none, 1 open, 2 short, 3 stuck on */
  vr_v_amp,      /* output: thermocouple amplifier output [V] */
  vr_i_heater,   /* output: heater current [A] */
  vr_t_heater_c, /* output [degC] */
  vr_t_tip_c,    /* output [degC] */
  vr_t_handle_c, /* output [degC], what the handle NTC reads */
  vr_c_h,        /* parameters, see t12_params_t */
  vr_c_t,
  vr_c_c,
  vr_r_ht,
  vr_r_ta,
  vr_r_tc,
  vr_r_ca,
  vr_v_supply,
  vr_r_coil,
  vr_r_short,
  vr_seebeck,
  vr_amp_gain,
  vr_amp_offset,
  vr_v_clamp,
  vr_tau_sense
} ValueReference;

typedef struct {
  double heater, series_on, ambient_c, load_wk, fault; /* inputs */
  t12_params_t par;    /* parameters; g_load follows the load_wk input */
  bool coeffs_changed; /* a parameter or the load changed: recompute the coefficients */
  t12_plant_t sim;
} ModelData;

#endif /* config_h */
